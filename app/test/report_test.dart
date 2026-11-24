import 'package:asthmaguard/data/models.dart';
import 'package:asthmaguard/report/pdf_report.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test('report summary skips false alarms for vitals and counts doses', () {
    final t = DateTime(2026, 11, 1, 8);
    final eps = [
      Episode(id: 1, start: t, end: t.add(const Duration(minutes: 10)), doses: 1, minSpo2: 91, maxHr: 125, emergencySent: true),
      Episode(id: 2, start: t, end: t.add(const Duration(minutes: 20)), doses: 2, minSpo2: 93, maxHr: 118),
      Episode(id: 3, start: t, end: t.add(const Duration(seconds: 8)), minSpo2: 80, maxHr: 160, falseAlarm: true),
    ];
    final s = summarize(eps, t);
    expect(s.episodes, 2);
    expect(s.falseAlarms, 1);
    expect(s.emergencies, 1);
    expect(s.doses, 3);
    expect(s.minSpo2, 91);
    expect(s.maxHr, 125);
    expect(s.avgDuration, const Duration(minutes: 15));
  });
}
