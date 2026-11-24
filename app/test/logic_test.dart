import 'dart:convert';

import 'package:asthmaguard/core/calibration.dart';
import 'package:asthmaguard/core/config_fields.dart';
import 'package:asthmaguard/core/emergency.dart';
import 'package:asthmaguard/core/episode_tracker.dart';
import 'package:asthmaguard/core/protocol.dart';
import 'package:asthmaguard/data/models.dart';
import 'package:asthmaguard/data/store.dart';
import 'package:flutter_test/flutter_test.dart';

final t0 = DateTime(2026, 11, 24, 10);

DeviceEvent ev(int id, String type, {int sec = 0, Map<String, dynamic>? data}) => DeviceEvent(
      id: id,
      epoch: t0.add(Duration(seconds: sec)).millisecondsSinceEpoch ~/ 1000,
      uptime: sec,
      type: type,
      data: data ?? {'hr': 120, 'spo2': 93, 'v': 1, 'c60': 5},
      receivedAt: t0.add(Duration(seconds: sec)),
    );

LiveData live(int sec, {String st = 'NORMAL', int hr = 75, int spo2 = 97, bool valid = true, int c60 = 0}) => LiveData(
      hr: hr,
      spo2: spo2,
      valid: valid,
      cough60: c60,
      activity: 'REST',
      state: st,
      battery: 80,
      sim: false,
      pending: 0,
      doseEnabled: true,
      at: t0.add(Duration(seconds: sec)),
    );

void main() {
  group('protocol', () {
    test('parses live JSON from the firmware', () {
      final l = LiveData.tryParse(
          '{"hr":88,"spo2":97,"v":1,"c60":0,"act":"REST","st":"NORMAL","bat":82,"sim":0,"pend":2,"dose":1}', t0)!;
      expect(l.hr, 88);
      expect(l.valid, isTrue);
      expect(l.state, 'NORMAL');
      expect(l.pending, 2);
      expect(l.doseEnabled, isTrue);
      expect(LiveData.tryParse('garbage', t0), isNull);
      expect(LiveData.tryParse('{"hr":1}', t0), isNull);
    });

    test('bat -1 or missing means no battery (Zero 2 W on USB power)', () {
      final usb = LiveData.tryParse('{"hr":88,"spo2":97,"v":1,"c60":0,"act":"REST","st":"NORMAL","bat":-1}', t0)!;
      expect(usb.hasBattery, isFalse);
      final missing = LiveData.tryParse('{"st":"NORMAL"}', t0)!;
      expect(missing.hasBattery, isFalse);
      final lipo = LiveData.tryParse('{"st":"NORMAL","bat":0}', t0)!;
      expect(lipo.hasBattery, isTrue);
    });

    test('parses event JSON and falls back to receive time without a clock', () {
      final e = DeviceEvent.tryParse(
          '{"id":123,"t":0,"up":50,"type":"DOSE","data":{"hr":110,"spo2":95,"v":1,"c60":4,"n":1,"max":2}}', t0)!;
      expect(e.id, 123);
      expect(e.type, 'DOSE');
      expect(e.time, t0);
      expect(e.data['n'], 1);
      final e2 = DeviceEvent.tryParse('{"id":5,"t":1764000000,"up":1,"type":"EMERGENCY","data":{}}', t0)!;
      expect(e2.time, DateTime.fromMillisecondsSinceEpoch(1764000000 * 1000));
      expect(e2.isEmergency, isTrue);
      expect(DeviceEvent.tryParse('{"type":"X"}', t0), isNull);
    });

    test('config writes are chunked under the BLE write size', () {
      final m = {for (var i = 0; i < 30; i++) 'key_number_$i': i * 1.5};
      final parts = chunkConfig(m, maxBytes: 100);
      expect(parts.length, greaterThan(1));
      final merged = <String, dynamic>{};
      for (final p in parts) {
        expect(utf8.encode(p).length, lessThanOrEqualTo(100));
        merged.addAll(jsonDecode(p) as Map<String, dynamic>);
      }
      expect(merged.length, 30);
    });
  });

  group('episode tracker', () {
    test('attack -> dose -> resolved', () async {
      final s = MemoryStore();
      final tr = EpisodeTracker(s);
      expect(await tr.onEvent(ev(1, 'SUSPECT')), isTrue);
      expect(await s.openEpisode(), isNull); // SUSPECT alone is no episode
      await tr.onEvent(ev(2, 'ATTACK_START', sec: 30));
      final open = await s.openEpisode();
      expect(open, isNotNull);
      await tr.onEvent(ev(3, 'ARM_RAISED', sec: 50));
      await tr.onEvent(ev(4, 'DOSE', sec: 52, data: {'hr': 125, 'spo2': 91, 'v': 1, 'c60': 7, 'n': 1}));
      expect(await tr.onEvent(ev(4, 'DOSE', sec: 62)), isFalse); // re-sent before ACK
      await tr.onEvent(ev(5, 'RESOLVED', sec: 400, data: {'hr': 80, 'spo2': 96, 'v': 1, 'c60': 0}));
      final eps = await s.episodes();
      expect(eps.length, 1);
      final e = eps.first;
      expect(e.open, isFalse);
      expect(e.doses, 1);
      expect(e.minSpo2, 91);
      expect(e.maxHr, 125);
      expect(e.coughs, 7);
      expect(e.duration(t0).inSeconds, 370);
      expect((await s.doses(e.id!)).length, 1);
      expect((await s.events(episodeId: e.id)).map((x) => x.type), ['ATTACK_START', 'ARM_RAISED', 'DOSE', 'RESOLVED']);
    });

    test('false alarm and manual SOS', () async {
      final s = MemoryStore();
      final tr = EpisodeTracker(s);
      await tr.onEvent(ev(1, 'ATTACK_START'));
      await tr.onEvent(ev(2, 'FALSE_ALARM', sec: 5));
      expect((await s.episodes()).first.falseAlarm, isTrue);
      await tr.onEvent(ev(3, 'MANUAL_SOS', sec: 100));
      final ep = (await s.openEpisode())!;
      expect(ep.stateMax, 'EMERGENCY');
      await tr.markEmergencySent();
      expect((await s.openEpisode())!.emergencySent, isTrue);
    });

    test('invalid readings never set min/max', () async {
      final s = MemoryStore();
      final tr = EpisodeTracker(s);
      await tr.onEvent(ev(1, 'ATTACK_START', data: {'hr': 0, 'spo2': 0, 'v': 0, 'c60': 5}));
      await tr.onLive(live(1, st: 'ALERT', hr: 200, spo2: 70, valid: false));
      final ep = (await s.openEpisode())!;
      expect(ep.minSpo2, isNull);
      expect(ep.maxHr, isNull);
    });

    test('readings every 60 s normally and every 5 s in an episode', () async {
      final s = MemoryStore();
      final tr = EpisodeTracker(s);
      for (var i = 0; i < 300; i++) {
        await tr.onLive(live(i));
      }
      expect((await s.readings(t0, t0.add(const Duration(hours: 1)))).length, 5);
      for (var i = 300; i < 360; i++) {
        await tr.onLive(live(i, st: 'ALERT', hr: 120));
      }
      final all = await s.readings(t0, t0.add(const Duration(hours: 1)));
      expect(all.length, 5 + 12);
      // live state opened an episode even without the start event
      expect((await s.openEpisode())!.maxHr, 120);
    });
  });

  group('emergency', () {
    test('SMS text follows the spec template', () {
      final m = emergencyMessage(name: 'أحمد', fix: const GeoFix(30.0444, 31.2357), spo2: 89, hr: 128);
      expect(m, 'تنبيه طارئ: أحمد بيمر بأزمة ربو ومحتاج مساعدة فورًا. الموقع: https://maps.google.com/?q=30.044400,31.235700 — الأكسجين: 89% النبض: 128');
      expect(emergencyMessage(name: 'x'), contains('الموقع: غير متاح'));
    });

    test('sends to every contact, ACKs, and retries only the failed ones', () async {
      var now = t0;
      final sent = <String>[];
      var failPhone = '222';
      final c = EmergencyCoordinator(
        locate: () async => const GeoFix(1, 2),
        sendSms: (p, t) async {
          if (p == failPhone) return false;
          sent.add(p);
          return true;
        },
        now: () => now,
      );
      final contacts = [(name: 'a', phone: '111'), (name: 'b', phone: '222')];
      final e = ev(9, 'EMERGENCY');
      final r1 = (await c.handle(e, patientName: 'p', contacts: contacts))!;
      expect(r1.shouldAck, isTrue);
      expect(r1.sent, 1);
      expect(r1.failed, 1);
      now = now.add(const Duration(seconds: 10)); // device re-sends after 10 s
      expect(await c.handle(e, patientName: 'p', contacts: contacts), isNull); // retry gap
      now = now.add(const Duration(seconds: 30));
      failPhone = '';
      final r2 = (await c.handle(e, patientName: 'p', contacts: contacts))!;
      expect(r2.sent, 2);
      expect(sent, ['111', '222']); // 111 not messaged twice
    });

    test('no contact reached -> no ACK; old events are not sent', () async {
      final c = EmergencyCoordinator(locate: () async => null, sendSms: (p, t) async => false, now: () => t0);
      final r = (await c.handle(ev(1, 'MANUAL_SOS'), patientName: 'p', contacts: [(name: 'a', phone: '1')]))!;
      expect(r.shouldAck, isFalse);
      final late = EmergencyCoordinator(
          locate: () async => null, sendSms: (p, t) async => true, now: () => t0.add(const Duration(hours: 2)));
      final r2 = (await late.handle(ev(2, 'EMERGENCY'), patientName: 'p', contacts: [(name: 'a', phone: '1')]))!;
      expect(r2.late, isTrue);
      expect(r2.sent, 0);
      expect(r2.shouldAck, isTrue);
    });
  });

  group('calibration', () {
    test('median of valid resting readings', () {
      final rs = [
        for (var i = 0; i < 40; i++)
          Reading(time: t0, hr: 60 + (i % 5), spo2: 96 + (i % 3), valid: true, cough60: 0, activity: 'REST', state: 'NORMAL'),
        for (var i = 0; i < 40; i++)
          Reading(time: t0, hr: 150, spo2: 90, valid: false, cough60: 0, activity: 'REST', state: 'NORMAL'),
        for (var i = 0; i < 40; i++)
          Reading(time: t0, hr: 140, spo2: 97, valid: true, cough60: 0, activity: 'EXERCISE', state: 'NORMAL'),
      ];
      final b = computeBaseline(rs)!;
      expect(b.hr, 62);
      expect(b.spo2, 97);
      expect(b.samples, 40);
      expect(computeBaseline(rs.take(10)), isNull);
      final p = Patient(name: 'p', age: 30, doctorName: 'd', createdAt: t0);
      expect(calibrationDue(p, t0.add(const Duration(days: 2))), isFalse);
      expect(calibrationDue(p, t0.add(const Duration(days: 3))), isTrue);
    });
  });

  group('doctor limits', () {
    test('dosing needs both limits', () {
      expect(validateDoseLimits(2, 14400), isNull);
      expect(validateDoseLimits(0, 0), isNull);
      expect(validateDoseLimits(2, 0), 'both');
      expect(validateDoseLimits(9, 10), 'max_doses');
      for (final g in configGroups) {
        for (final f in g.fields) {
          expect(f.min < f.max, isTrue, reason: f.key);
        }
      }
    });
  });
}
