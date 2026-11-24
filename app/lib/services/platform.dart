// Platform helpers: permissions, GPS, SMS, phone call, background service.
import 'dart:async';
import 'dart:io';

import 'package:flutter/services.dart';
import 'package:flutter_foreground_task/flutter_foreground_task.dart';
import 'package:geolocator/geolocator.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:url_launcher/url_launcher.dart';

import '../core/emergency.dart';

const ambulanceNumber = '123'; // Egypt

class PlatformServices {
  static const _sms = MethodChannel('asthmaguard/sms'); // MainActivity.kt -> SmsManager

  Future<Map<String, bool>> requestPermissions() async {
    final perms = <Permission>[
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.locationWhenInUse,
      Permission.notification,
      if (Platform.isAndroid) Permission.sms,
    ];
    final res = await perms.request();
    return {for (final e in res.entries) e.key.toString(): e.value.isGranted};
  }

  Future<GeoFix?> locate() async {
    try {
      if (!await Geolocator.isLocationServiceEnabled()) return await _lastKnown();
      final p = await Geolocator.getCurrentPosition(
        locationSettings: const LocationSettings(accuracy: LocationAccuracy.high, timeLimit: Duration(seconds: 12)),
      );
      return GeoFix(p.latitude, p.longitude);
    } catch (_) {
      return await _lastKnown();
    }
  }

  Future<GeoFix?> _lastKnown() async {
    try {
      final p = await Geolocator.getLastKnownPosition();
      return p == null ? null : GeoFix(p.latitude, p.longitude, lastKnown: true);
    } catch (_) {
      return null;
    }
  }

  /// Android: sends silently and waits for the SENT status.
  /// iOS: silent SMS is not allowed; opens a pre-filled message (one tap).
  Future<bool> sendSms(String phone, String text) async {
    if (Platform.isAndroid) {
      try {
        final ok = await _sms
            .invokeMethod<bool>('send', {'phone': phone, 'text': text})
            .timeout(const Duration(seconds: 30), onTimeout: () => false);
        return ok ?? false;
      } catch (_) {
        return false;
      }
    }
    // iOS format: sms:<phone>&body=<text> (spaces must be %20, not +)
    return launchUrl(Uri.parse('sms:$phone&body=${Uri.encodeComponent(text)}'));
  }

  Future<void> callAmbulance() => launchUrl(Uri(scheme: 'tel', path: ambulanceNumber));

  Future<void> openMap(double lat, double lng) =>
      launchUrl(Uri.parse('https://maps.google.com/?q=$lat,$lng'), mode: LaunchMode.externalApplication);

  // ---------------------------------------------------------------- background
  void initBackground() {
    FlutterForegroundTask.init(
      androidNotificationOptions: AndroidNotificationOptions(
        channelId: 'asthmaguard_link',
        channelName: 'AsthmaGuard',
        channelDescription: 'Keeps the wristband connected',
      ),
      iosNotificationOptions: const IOSNotificationOptions(showNotification: false),
      foregroundTaskOptions: ForegroundTaskOptions(
        eventAction: ForegroundTaskEventAction.repeat(60000),
        autoRunOnBoot: true,
        allowWakeLock: true,
      ),
    );
  }

  Future<void> startBackground({required String title, required String text}) async {
    if (!Platform.isAndroid) return;
    if (await FlutterForegroundTask.isRunningService) return;
    await FlutterForegroundTask.startService(
      serviceId: 7,
      serviceTypes: [ForegroundServiceTypes.connectedDevice, ForegroundServiceTypes.location],
      notificationTitle: title,
      notificationText: text,
      callback: startKeepAliveHandler,
    );
  }

  Future<void> updateBackground({required String title, required String text}) async {
    if (!Platform.isAndroid || !await FlutterForegroundTask.isRunningService) return;
    await FlutterForegroundTask.updateService(notificationTitle: title, notificationText: text);
  }
}

// The service only keeps the app process (and its BLE link, which lives in
// the main isolate) alive while the screen is off.
@pragma('vm:entry-point')
void startKeepAliveHandler() => FlutterForegroundTask.setTaskHandler(_KeepAlive());

class _KeepAlive extends TaskHandler {
  @override
  Future<void> onStart(DateTime timestamp, TaskStarter starter) async {}
  @override
  void onRepeatEvent(DateTime timestamp) {}
  @override
  Future<void> onDestroy(DateTime timestamp, bool isTimeout) async {}
}
