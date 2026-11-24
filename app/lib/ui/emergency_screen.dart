import 'package:flutter/material.dart';

import '../core/protocol.dart';
import '../l10n.dart';
import 'scope.dart';

class EmergencyScreen extends StatelessWidget {
  final DeviceEvent event;
  const EmergencyScreen({super.key, required this.event});

  @override
  Widget build(BuildContext context) {
    final c = AppScope.of(context);
    final l = L.of(context);
    final r = c.lastEmergency;
    final live = c.live;
    const red = Color(0xFFB00020);
    return Scaffold(
      backgroundColor: red,
      body: SafeArea(
        child: Padding(
          padding: const EdgeInsets.all(20),
          child: Column(crossAxisAlignment: CrossAxisAlignment.stretch, children: [
            const Icon(Icons.warning_amber_rounded, color: Colors.white, size: 72),
            const SizedBox(height: 8),
            Text(l.t('emergency_title'),
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.headlineMedium?.copyWith(color: Colors.white, fontWeight: FontWeight.bold)),
            const SizedBox(height: 16),
            if (live != null && live.valid)
              Text('${l.t('spo2')}: ${live.spo2}%   ${l.t('hr')}: ${live.hr}',
                  textAlign: TextAlign.center, style: const TextStyle(color: Colors.white, fontSize: 20)),
            const SizedBox(height: 16),
            Card(
              child: Padding(
                padding: const EdgeInsets.all(14),
                child: c.emergencyBusy || r == null
                    ? Row(children: [
                        const SizedBox(width: 22, height: 22, child: CircularProgressIndicator(strokeWidth: 2)),
                        const SizedBox(width: 12),
                        Expanded(child: Text(l.t('sending_sms'))),
                      ])
                    : Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                        if (r.sent > 0) Text('${l.t('sms_sent_to')} ${r.sent}', style: const TextStyle(fontWeight: FontWeight.bold)),
                        if (r.failed > 0) Text('${l.t('sms_failed')}: ${r.failed}', style: const TextStyle(color: red)),
                        if (r.message.isNotEmpty) ...[const SizedBox(height: 8), Text(r.message)],
                      ]),
              ),
            ),
            const Spacer(),
            FilledButton.icon(
              style: FilledButton.styleFrom(
                  backgroundColor: Colors.white, foregroundColor: red, padding: const EdgeInsets.symmetric(vertical: 22)),
              icon: const Icon(Icons.call, size: 30),
              label: Text(l.t('call_123'), style: const TextStyle(fontSize: 22, fontWeight: FontWeight.bold)),
              onPressed: c.platform.callAmbulance,
            ),
            const SizedBox(height: 12),
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: Text(l.t('close'), style: const TextStyle(color: Colors.white)),
            ),
          ]),
        ),
      ),
    );
  }
}
