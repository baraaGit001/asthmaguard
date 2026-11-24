import 'dart:convert';

import 'package:fl_chart/fl_chart.dart';
import 'package:flutter/material.dart';
import 'package:intl/intl.dart' show DateFormat;

import '../data/models.dart';
import '../l10n.dart';
import 'history_screen.dart';
import 'scope.dart';

class EpisodeScreen extends StatelessWidget {
  final int episodeId;
  const EpisodeScreen({super.key, required this.episodeId});

  Future<(Episode?, List<Reading>, List<EventRow>)> _load(BuildContext context) async {
    final s = AppScope.read(context).store;
    final ep = await s.episode(episodeId);
    if (ep == null) return (null, <Reading>[], <EventRow>[]);
    final from = ep.start.subtract(const Duration(minutes: 3));
    final to = (ep.end ?? DateTime.now()).add(const Duration(minutes: 3));
    return (ep, await s.readings(from, to), await s.events(episodeId: episodeId));
  }

  @override
  Widget build(BuildContext context) {
    final l = L.of(context);
    return Scaffold(
      appBar: AppBar(title: Text(l.t('episode'))),
      body: FutureBuilder(
        future: _load(context),
        builder: (context, snap) {
          if (!snap.hasData) return const Center(child: CircularProgressIndicator());
          final (ep, readings, events) = snap.data!;
          if (ep == null) return const SizedBox();
          final t0 = ep.start.subtract(const Duration(minutes: 3));
          double x(DateTime t) => t.difference(t0).inSeconds / 60.0;
          final valid = readings.where((r) => r.valid).toList();
          final marks = [
            for (final e in events)
              if (e.type != 'SUSPECT') (x: x(e.time), type: e.type),
          ];
          return ListView(padding: const EdgeInsets.all(12), children: [
            EpisodeCard(ep: ep),
            _Chart(title: '${l.t('hr')} (bpm)', color: Colors.red, spots: [for (final r in valid) FlSpot(x(r.time), r.hr.toDouble())], marks: marks),
            _Chart(title: '${l.t('spo2')} (%)', color: Colors.blue, spots: [for (final r in valid) FlSpot(x(r.time), r.spo2.toDouble())], marks: marks, minY: 80, maxY: 100),
            _Chart(title: l.t('coughs_min'), color: Colors.orange, spots: [for (final r in readings) FlSpot(x(r.time), r.cough60.toDouble())], marks: marks, minY: 0),
            const SizedBox(height: 8),
            Text(l.t('events'), style: Theme.of(context).textTheme.titleMedium),
            for (final e in events)
              ListTile(
                dense: true,
                leading: Icon(_icon(e.type), color: _markColor(e.type)),
                title: Text(e.type),
                subtitle: Text(_summary(e.payloadJson), textDirection: TextDirection.ltr),
                trailing: Text(DateFormat.Hms().format(e.time)),
              ),
          ]);
        },
      ),
    );
  }

  static String _summary(String payload) {
    try {
      final j = jsonDecode(payload) as Map<String, dynamic>;
      final d = (j['data'] as Map?) ?? {};
      return d.entries.map((e) => '${e.key}=${e.value}').join('  ');
    } catch (_) {
      return '';
    }
  }
}

IconData _icon(String t) => switch (t) {
      'DOSE' => Icons.medication,
      'EMERGENCY' || 'MANUAL_SOS' => Icons.sos,
      'ARM_RAISED' => Icons.back_hand,
      'RESOLVED' || 'IMPROVED' => Icons.check_circle,
      'FALSE_ALARM' => Icons.cancel,
      'DOSE_BLOCKED' => Icons.block,
      _ => Icons.circle_outlined,
    };

Color _markColor(String t) => switch (t) {
      'DOSE' => Colors.green.shade700,
      'EMERGENCY' || 'MANUAL_SOS' => const Color(0xFFB00020),
      'RESOLVED' || 'IMPROVED' => Colors.teal,
      _ => Colors.grey,
    };

class _Chart extends StatelessWidget {
  final String title;
  final Color color;
  final List<FlSpot> spots;
  final List<({double x, String type})> marks;
  final double? minY, maxY;
  const _Chart({required this.title, required this.color, required this.spots, required this.marks, this.minY, this.maxY});

  @override
  Widget build(BuildContext context) {
    return Card(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(8, 12, 16, 8),
        child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
          Padding(padding: const EdgeInsetsDirectional.only(start: 8), child: Text(title)),
          const SizedBox(height: 8),
          SizedBox(
            height: 160,
            child: spots.isEmpty
                ? const Center(child: Text('—'))
                : Directionality(
                    textDirection: TextDirection.ltr, // time runs left to right
                    child: LineChart(LineChartData(
                      minY: minY,
                      maxY: maxY,
                      lineBarsData: [
                        LineChartBarData(spots: spots, color: color, barWidth: 2, dotData: const FlDotData(show: false)),
                      ],
                      extraLinesData: ExtraLinesData(verticalLines: [
                        for (final m in marks)
                          if (m.type == 'DOSE' || m.type == 'EMERGENCY' || m.type == 'MANUAL_SOS' || m.type == 'ATTACK_START' || m.type == 'RESOLVED')
                            VerticalLine(x: m.x, color: _markColor(m.type), strokeWidth: m.type == 'DOSE' ? 2.5 : 1.5, dashArray: m.type == 'DOSE' ? null : [4, 4]),
                      ]),
                      titlesData: const FlTitlesData(
                        topTitles: AxisTitles(sideTitles: SideTitles(showTitles: false)),
                        rightTitles: AxisTitles(sideTitles: SideTitles(showTitles: false)),
                        bottomTitles: AxisTitles(sideTitles: SideTitles(showTitles: true, reservedSize: 22, interval: 5)),
                        leftTitles: AxisTitles(sideTitles: SideTitles(showTitles: true, reservedSize: 34)),
                      ),
                      gridData: const FlGridData(drawVerticalLine: false),
                      borderData: FlBorderData(show: false),
                    )),
                  ),
          ),
        ]),
      ),
    );
  }
}
