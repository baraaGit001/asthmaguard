import 'package:flutter/material.dart';

import '../app_controller.dart';

class AppScope extends InheritedNotifier<AppController> {
  const AppScope({super.key, required AppController controller, required super.child})
      : super(notifier: controller);

  static AppController of(BuildContext context) =>
      context.dependOnInheritedWidgetOfExactType<AppScope>()!.notifier!;
  static AppController read(BuildContext context) =>
      context.getInheritedWidgetOfExactType<AppScope>()!.notifier!;
}

/// Colour per device state (green / amber / red / deep red).
Color stateColor(String st, ColorScheme cs) {
  switch (st) {
    case 'SUSPECT':
      return const Color(0xFFE0A100);
    case 'ALERT':
    case 'WAIT_RAISE':
    case 'WAIT_CONFIRM':
    case 'DOSE':
    case 'MONITOR':
      return const Color(0xFFD9480F);
    case 'EMERGENCY':
      return const Color(0xFFB00020);
    default:
      return const Color(0xFF2B8A3E);
  }
}

/// Big status group of section 6.2: Normal / Suspect / Attack / Emergency.
String statusGroup(String st) {
  switch (st) {
    case 'SUSPECT':
      return 'SUSPECT';
    case 'EMERGENCY':
      return 'EMERGENCY';
    case 'ALERT':
    case 'WAIT_RAISE':
    case 'WAIT_CONFIRM':
    case 'DOSE':
    case 'MONITOR':
      return 'ATTACK';
    default:
      return 'NORMAL';
  }
}

String fmtDuration(Duration d, bool ar) {
  final m = d.inMinutes;
  final s = d.inSeconds % 60;
  if (m >= 60) return '${m ~/ 60}:${(m % 60).toString().padLeft(2, '0')} ${ar ? 'س' : 'h'}';
  return '$m:${s.toString().padLeft(2, '0')} ${ar ? 'د' : 'min'}';
}
