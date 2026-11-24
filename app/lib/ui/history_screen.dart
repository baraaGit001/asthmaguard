import 'package:flutter/material.dart';
import 'package:intl/intl.dart';

import '../data/models.dart';
import '../l10n.dart';
import 'episode_screen.dart';
import 'scope.dart';

class HistoryScreen extends StatelessWidget {
  const HistoryScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final c = AppScope.of(context); // rebuilds when new events arrive
    final l = L.of(context);
    return FutureBuilder<List<Episode>>(
      future: c.store.episodes(),
      builder: (context, snap) {
        final eps = snap.data ?? const [];
        if (snap.connectionState != ConnectionState.done && eps.isEmpty) {
          return const Center(child: CircularProgressIndicator());
        }
        if (eps.isEmpty) return Center(child: Text(l.t('no_episodes')));
        return ListView.separated(
          padding: const EdgeInsets.all(12),
          itemCount: eps.length,
          separatorBuilder: (_, _) => const SizedBox(height: 4),
          itemBuilder: (context, i) => EpisodeCard(ep: eps[i]),
        );
      },
    );
  }
}

class EpisodeCard extends StatelessWidget {
  final Episode ep;
  const EpisodeCard({super.key, required this.ep});

  @override
  Widget build(BuildContext context) {
    final l = L.of(context);
    final theme = Theme.of(context);
    final lc = l.ar ? 'ar' : 'en';
    final date = DateFormat.yMMMd(lc).format(ep.start);
    final from = DateFormat.Hm(lc).format(ep.start);
    final to = ep.end == null ? l.t('ongoing') : DateFormat.Hm(lc).format(ep.end!);
    final color = ep.falseAlarm ? Colors.grey : stateColor(ep.stateMax, theme.colorScheme);
    return Card(
      child: InkWell(
        borderRadius: BorderRadius.circular(12),
        onTap: () => Navigator.push(context, MaterialPageRoute(builder: (_) => EpisodeScreen(episodeId: ep.id!))),
        child: Padding(
          padding: const EdgeInsets.all(12),
          child: Row(children: [
            Container(width: 6, height: 64, decoration: BoxDecoration(color: color, borderRadius: BorderRadius.circular(3))),
            const SizedBox(width: 12),
            Expanded(
              child: Column(crossAxisAlignment: CrossAxisAlignment.start, children: [
                Text('$date   $from – $to', style: theme.textTheme.titleSmall),
                const SizedBox(height: 4),
                Wrap(spacing: 12, runSpacing: 2, children: [
                  Text('${l.t('duration')}: ${fmtDuration(ep.duration(DateTime.now()), l.ar)}'),
                  Text('${l.t('doses')}: ${ep.doses}'),
                  Text('${l.t('min_spo2')}: ${ep.minSpo2 ?? '—'}%'),
                  Text('${l.t('max_hr')}: ${ep.maxHr ?? '—'}'),
                ]),
                const SizedBox(height: 4),
                Wrap(spacing: 6, children: [
                  if (ep.emergencySent) _Tag(l.t('emergency_sent'), const Color(0xFFB00020)),
                  if (ep.falseAlarm) _Tag(l.t('false_alarm'), Colors.grey),
                ]),
              ]),
            ),
            const Icon(Icons.chevron_right),
          ]),
        ),
      ),
    );
  }
}

class _Tag extends StatelessWidget {
  final String text;
  final Color color;
  const _Tag(this.text, this.color);
  @override
  Widget build(BuildContext context) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
        decoration: BoxDecoration(color: color.withValues(alpha: 0.15), borderRadius: BorderRadius.circular(8)),
        child: Text(text, style: TextStyle(color: color, fontSize: 12)),
      );
}
