import 'package:flutter/material.dart';

import '../core/calibration.dart';
import '../l10n.dart';
import '../services/device_service.dart';
import 'scope.dart';

class HomeScreen extends StatelessWidget {
  const HomeScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final c = AppScope.of(context);
    final l = L.of(context);
    final theme = Theme.of(context);
    final live = c.live;
    final connected = c.link == LinkState.connected;
    final st = live?.state ?? 'NORMAL';
    final color = stateColor(st, theme.colorScheme);
    final group = statusGroup(st);
    final calibrating = c.patient != null &&
        c.patient!.calibratedAt == null &&
        DateTime.now().difference(c.patient!.createdAt) < calibrationPeriod;

    return ListView(
      padding: const EdgeInsets.all(16),
      children: [
        Row(children: [
          Expanded(
            child: Text(c.patient?.name ?? '', style: theme.textTheme.titleLarge, overflow: TextOverflow.ellipsis),
          ),
          _ConnChip(state: c.link),
        ]),
        const SizedBox(height: 12),
        // Big status
        AnimatedContainer(
          duration: const Duration(milliseconds: 300),
          padding: const EdgeInsets.symmetric(vertical: 28, horizontal: 16),
          decoration: BoxDecoration(
            color: connected ? color : theme.colorScheme.surfaceContainerHighest,
            borderRadius: BorderRadius.circular(24),
          ),
          child: Column(children: [
            Text(
              connected ? l.t('status_$group') : l.t('disconnected'),
              style: theme.textTheme.displaySmall?.copyWith(
                  color: connected ? Colors.white : theme.colorScheme.onSurfaceVariant, fontWeight: FontWeight.bold),
            ),
            if (connected && group != 'NORMAL') ...[
              const SizedBox(height: 6),
              Text(l.state(st), style: theme.textTheme.titleMedium?.copyWith(color: Colors.white)),
            ],
          ]),
        ),
        const SizedBox(height: 16),
        Row(children: [
          Expanded(
            child: _Tile(
              icon: Icons.favorite,
              label: l.t('hr'),
              value: live == null ? '—' : (live.valid ? '${live.hr}' : l.t('measuring')),
              unit: live != null && live.valid ? 'bpm' : '',
            ),
          ),
          const SizedBox(width: 12),
          Expanded(
            child: _Tile(
              icon: Icons.air,
              label: l.t('spo2'),
              value: live == null ? '—' : (live.valid ? '${live.spo2}' : l.t('measuring')),
              unit: live != null && live.valid ? '%' : '',
            ),
          ),
        ]),
        const SizedBox(height: 12),
        Row(children: [
          Expanded(
            child: _Tile(icon: Icons.graphic_eq, label: l.t('coughs_min'), value: live == null ? '—' : '${live.cough60}'),
          ),
          // The Zero 2 W runs from USB power: no battery tile unless the
          // device reports a level (bat >= 0).
          if (live != null && live.hasBattery) ...[
            const SizedBox(width: 12),
            Expanded(
              child: _Tile(
                icon: _batteryIcon(live.battery),
                label: l.t('battery'),
                value: '${live.battery}',
                unit: '%',
              ),
            ),
          ],
        ]),
        if (live != null) ...[
          const SizedBox(height: 8),
          Text('${l.activity(live.activity)}  •  ${l.state(live.state)}',
              textAlign: TextAlign.center, style: theme.textTheme.bodySmall),
        ],
        const SizedBox(height: 12),
        if (live?.sim ?? false) _Banner(icon: Icons.science, text: l.t('sim_on'), color: Colors.purple),
        if (live != null && !live.doseEnabled) _Banner(icon: Icons.medication_outlined, text: l.t('dose_off'), color: Colors.blueGrey),
        if (calibrating) _Banner(icon: Icons.tune, text: l.t('calibrating'), color: Colors.teal),
        const SizedBox(height: 16),
        FilledButton.icon(
          style: FilledButton.styleFrom(
            backgroundColor: const Color(0xFFB00020),
            padding: const EdgeInsets.symmetric(vertical: 18),
          ),
          icon: const Icon(Icons.local_hospital),
          label: Text(l.t('call_123'), style: const TextStyle(fontSize: 18)),
          onPressed: c.platform.callAmbulance,
        ),
      ],
    );
  }

  IconData _batteryIcon(int? b) {
    if (b == null) return Icons.battery_unknown;
    if (b > 80) return Icons.battery_full;
    if (b > 40) return Icons.battery_5_bar;
    if (b > 15) return Icons.battery_3_bar;
    return Icons.battery_alert;
  }
}

class _Tile extends StatelessWidget {
  final IconData icon;
  final String label;
  final String value;
  final String unit;
  const _Tile({required this.icon, required this.label, required this.value, this.unit = ''});

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(14),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Row(children: [
            Icon(icon, size: 18, color: theme.colorScheme.primary),
            const SizedBox(width: 6),
            Expanded(child: Text(label, style: theme.textTheme.labelLarge, overflow: TextOverflow.ellipsis)),
          ]),
          const SizedBox(height: 8),
          Row(crossAxisAlignment: CrossAxisAlignment.end, children: [
            Flexible(
              child: Text(value,
                  style: value.length > 4 ? theme.textTheme.titleMedium : theme.textTheme.headlineMedium,
                  overflow: TextOverflow.ellipsis),
            ),
            if (unit.isNotEmpty) Padding(padding: const EdgeInsetsDirectional.only(start: 4, bottom: 4), child: Text(unit)),
          ]),
        ]),
      ),
    );
  }
}

class _Banner extends StatelessWidget {
  final IconData icon;
  final String text;
  final Color color;
  const _Banner({required this.icon, required this.text, required this.color});
  @override
  Widget build(BuildContext context) => Card(
        color: color.withValues(alpha: 0.12),
        child: ListTile(leading: Icon(icon, color: color), title: Text(text), dense: true),
      );
}

class _ConnChip extends StatelessWidget {
  final LinkState state;
  const _ConnChip({required this.state});
  @override
  Widget build(BuildContext context) {
    final l = L.of(context);
    final (icon, text, color) = switch (state) {
      LinkState.connected => (Icons.bluetooth_connected, l.t('connected'), Colors.green),
      LinkState.connecting => (Icons.bluetooth_searching, l.t('connecting'), Colors.orange),
      LinkState.disconnected => (Icons.bluetooth_disabled, l.t('disconnected'), Colors.grey),
    };
    return Chip(avatar: Icon(icon, size: 18, color: color), label: Text(text));
  }
}
