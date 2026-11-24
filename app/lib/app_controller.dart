// App state + glue between the wristband, storage and the emergency flow.
import 'dart:async';

import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';

import 'core/calibration.dart';
import 'core/emergency.dart';
import 'core/episode_tracker.dart';
import 'core/protocol.dart';
import 'data/models.dart';
import 'data/store.dart';
import 'services/device_service.dart';
import 'services/platform.dart';

class AppController extends ChangeNotifier {
  final Store store;
  final DeviceService device;
  final PlatformServices platform;
  late final EpisodeTracker tracker = EpisodeTracker(store);
  late final EmergencyCoordinator emergency =
      EmergencyCoordinator(locate: platform.locate, sendSms: platform.sendSms);
  final navigatorKey = GlobalKey<NavigatorState>();

  Patient? patient;
  List<Contact> contacts = [];
  LiveData? live;
  LinkState link = LinkState.disconnected;
  Locale locale = const Locale('ar');
  String? pairedId;
  EmergencyResult? lastEmergency;
  bool emergencyBusy = false;
  final _inProgress = <int>{};
  DateTime? _lastCalibrationCheck;
  String? _lastNotifiedState;

  /// Set by main.dart: shows the full-screen emergency alert.
  void Function(EmergencyResult? r, DeviceEvent e)? showEmergency;

  AppController({required this.store, required this.device, required this.platform});

  bool get onboarded => patient != null && contacts.isNotEmpty;

  Future<void> init() async {
    patient = await store.patient();
    contacts = await store.contacts();
    final prefs = await SharedPreferences.getInstance();
    pairedId = prefs.getString('paired_id');
    locale = Locale(prefs.getString('lang') ?? 'ar');
    platform.initBackground();
    device.live.listen(_onLive);
    device.events.listen(_onEvent);
    device.link.listen((s) {
      link = s;
      notifyListeners();
    });
    if (pairedId != null) unawaited(connect(pairedId!));
  }

  Future<void> setLocale(String lang) async {
    locale = Locale(lang);
    (await SharedPreferences.getInstance()).setString('lang', lang);
    notifyListeners();
  }

  Future<void> savePatient(Patient p, List<Contact> c) async {
    await store.savePatient(p);
    await store.replaceContacts(c);
    patient = p;
    contacts = await store.contacts();
    notifyListeners();
  }

  Future<void> connect(String id) async {
    pairedId = id;
    (await SharedPreferences.getInstance()).setString('paired_id', id);
    final ar = locale.languageCode == 'ar';
    await platform.startBackground(
      title: ar ? 'أزما جارد' : 'AsthmaGuard',
      text: ar ? 'السوار متصل ويراقب' : 'Wristband connected and watching',
    );
    await device.connectTo(id);
  }

  Future<void> unpair() async {
    await device.forget();
    pairedId = null;
    (await SharedPreferences.getInstance()).remove('paired_id');
    notifyListeners();
  }

  // ------------------------------------------------------------------ live
  Future<void> _onLive(LiveData l) async {
    live = l;
    notifyListeners();
    await tracker.onLive(l);
    if (l.state != _lastNotifiedState) {
      _lastNotifiedState = l.state;
      final ar = locale.languageCode == 'ar';
      await platform.updateBackground(
        title: ar ? 'أزما جارد' : 'AsthmaGuard',
        text: '${ar ? 'الحالة' : 'State'}: ${l.state}',
      );
    }
    final now = DateTime.now();
    if (_lastCalibrationCheck == null || now.difference(_lastCalibrationCheck!) > const Duration(minutes: 10)) {
      _lastCalibrationCheck = now;
      await _maybeCalibrate(now);
    }
  }

  Future<void> _maybeCalibrate(DateTime now) async {
    final p = patient;
    if (p == null || !calibrationDue(p, now) || link != LinkState.connected) return;
    final b = computeBaseline(await store.readings(p.createdAt, now));
    if (b == null) return; // keep collecting
    try {
      await device.writeConfig({
        'baseline_hr': double.parse(b.hr.toStringAsFixed(1)),
        'baseline_spo2': double.parse(b.spo2.toStringAsFixed(1)),
        'calibrated_at': now.millisecondsSinceEpoch ~/ 1000,
      });
      patient = p.copyWith(baselineHr: b.hr, baselineSpo2: b.spo2, calibratedAt: now);
      await store.savePatient(patient!);
      notifyListeners();
    } catch (_) {}
  }

  // ------------------------------------------------------------------ events
  Future<void> _onEvent(DeviceEvent e) async {
    if (_inProgress.contains(e.id)) return; // a re-send while we are still handling it
    _inProgress.add(e.id);
    try {
      await tracker.onEvent(e);
      if (!e.isEmergency) {
        await device.ack(e.id);
        notifyListeners();
        return;
      }
      emergencyBusy = true;
      notifyListeners();
      showEmergency?.call(null, e);
      final res = await emergency.handle(
        e,
        patientName: patient?.name ?? '',
        contacts: [for (final c in contacts) (name: c.name, phone: c.phone)],
        liveSpo2: (live?.valid ?? false) ? live!.spo2 : null,
        liveHr: (live?.valid ?? false) ? live!.hr : null,
      );
      emergencyBusy = false;
      if (res == null) return; // retry gap: wait for the device to re-send
      lastEmergency = res;
      if (!res.late) showEmergency?.call(res, e);
      if (res.shouldAck) {
        if (res.sent > 0) await tracker.markEmergencySent();
        await device.ack(e.id);
      }
      notifyListeners();
    } finally {
      _inProgress.remove(e.id);
    }
  }
}
