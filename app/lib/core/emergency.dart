// Emergency flow on the phone (section 6.5), with the platform parts
// (location, SMS) injected so the logic is testable.
import 'protocol.dart';

class GeoFix {
  final double lat;
  final double lng;
  final bool lastKnown;
  const GeoFix(this.lat, this.lng, {this.lastKnown = false});
}

/// The SMS text of section 6.5 (Arabic). Missing location / vitals are
/// stated, never left as placeholders.
String emergencyMessage({required String name, GeoFix? fix, int? spo2, int? hr}) {
  final loc = fix == null
      ? 'غير متاح'
      : 'https://maps.google.com/?q=${fix.lat.toStringAsFixed(6)},${fix.lng.toStringAsFixed(6)}';
  final o2 = (spo2 == null || spo2 <= 0) ? 'غير متاح' : '$spo2%';
  final pulse = (hr == null || hr <= 0) ? 'غير متاح' : '$hr';
  return 'تنبيه طارئ: $name بيمر بأزمة ربو ومحتاج مساعدة فورًا. الموقع: $loc — الأكسجين: $o2 النبض: $pulse';
}

typedef LocateFn = Future<GeoFix?> Function();
typedef SendSmsFn = Future<bool> Function(String phone, String text);

class EmergencyResult {
  final bool shouldAck;     // at least one contact got the SMS
  final int sent;
  final int failed;
  final bool late;          // event too old: logged, SMS not sent
  final String message;
  const EmergencyResult(this.shouldAck, this.sent, this.failed, this.late, this.message);
}

class EmergencyCoordinator {
  final LocateFn locate;
  final SendSmsFn sendSms;
  final Duration maxAge;
  final Duration retryGap;
  final DateTime Function() now;

  /// device event id -> phones already reached (so retries only send to the rest)
  final Map<int, Set<String>> _reached = {};
  final Map<int, DateTime> _lastAttempt = {};

  EmergencyCoordinator({
    required this.locate,
    required this.sendSms,
    this.maxAge = const Duration(minutes: 10),
    this.retryGap = const Duration(seconds: 30),
    DateTime Function()? now,
  }) : now = now ?? DateTime.now;

  /// Handles one EMERGENCY / MANUAL_SOS event. The device re-sends the
  /// event every 10 s until ACKed; repeated calls only retry contacts that
  /// were not reached, at most every [retryGap].
  Future<EmergencyResult?> handle(DeviceEvent e,
      {required String patientName, required List<({String name, String phone})> contacts, int? liveSpo2, int? liveHr}) async {
    final reached = _reached.putIfAbsent(e.id, () => <String>{});
    if (reached.isNotEmpty && reached.length >= contacts.length) {
      return EmergencyResult(true, reached.length, 0, false, '');
    }
    final t = now();
    if (now().difference(e.time) > maxAge) {
      // Delivered long after it happened (phone was away). Log it, do not
      // alarm the family about an old event; ACK so the device stops.
      return const EmergencyResult(true, 0, 0, true, '');
    }
    final last = _lastAttempt[e.id];
    if (last != null && t.difference(last) < retryGap) return null; // too soon, wait
    _lastAttempt[e.id] = t;

    GeoFix? fix;
    try {
      fix = await locate();
    } catch (_) {
      fix = null;
    }
    final text = emergencyMessage(
      name: patientName,
      fix: fix,
      spo2: (e.validReading ? e.spo2 : null) ?? liveSpo2,
      hr: (e.validReading ? e.hr : null) ?? liveHr,
    );
    var failed = 0;
    for (final c in contacts) {
      if (reached.contains(c.phone)) continue;
      bool ok;
      try {
        ok = await sendSms(c.phone, text);
      } catch (_) {
        ok = false;
      }
      if (ok) {
        reached.add(c.phone);
      } else {
        failed++;
      }
    }
    return EmergencyResult(reached.isNotEmpty, reached.length, failed, false, text);
  }
}
