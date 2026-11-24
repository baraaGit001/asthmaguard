// Doctor PDF report for a date range (section 6.2, doctor area).
import 'package:flutter/services.dart' show rootBundle, Uint8List;
import 'package:intl/intl.dart';
import 'package:pdf/pdf.dart';
import 'package:pdf/widgets.dart' as pw;

import '../data/models.dart';
import '../data/store.dart';

class ReportSummary {
  final int episodes;
  final int falseAlarms;
  final int emergencies;
  final int doses;
  final int? minSpo2;
  final int? maxHr;
  final Duration? avgDuration;
  const ReportSummary(this.episodes, this.falseAlarms, this.emergencies, this.doses, this.minSpo2, this.maxHr, this.avgDuration);
}

ReportSummary summarize(List<Episode> eps, DateTime now) {
  final real = eps.where((e) => !e.falseAlarm).toList();
  int? minS, maxH;
  for (final e in real) {
    if (e.minSpo2 != null && (minS == null || e.minSpo2! < minS)) minS = e.minSpo2;
    if (e.maxHr != null && (maxH == null || e.maxHr! > maxH)) maxH = e.maxHr;
  }
  final closed = real.where((e) => !e.open).toList();
  final avg = closed.isEmpty
      ? null
      : Duration(seconds: closed.map((e) => e.duration(now).inSeconds).reduce((a, b) => a + b) ~/ closed.length);
  return ReportSummary(
    real.length,
    eps.length - real.length,
    eps.where((e) => e.emergencySent).length,
    eps.fold(0, (s, e) => s + e.doses),
    minS,
    maxH,
    avg,
  );
}

String _dur(Duration? d) => d == null ? '—' : '${d.inMinutes}:${(d.inSeconds % 60).toString().padLeft(2, '0')}';

Future<Uint8List> buildReport({
  required Store store,
  required Patient patient,
  required DateTime from,
  required DateTime to,
  required Map<String, dynamic> deviceConfig,
  required bool arabic,
  DateTime? now,
}) async {
  now ??= DateTime.now();
  final eps = (await store.episodes(from: from, to: to)).reversed.toList(); // oldest first
  final s = summarize(eps, now);
  final regular = pw.Font.ttf(await rootBundle.load('assets/fonts/Amiri-Regular.ttf'));
  final bold = pw.Font.ttf(await rootBundle.load('assets/fonts/Amiri-Bold.ttf'));
  final lc = arabic ? 'ar' : 'en';
  final df = DateFormat('yyyy-MM-dd HH:mm');
  final dd = DateFormat('yyyy-MM-dd');
  String t(String ar, String en) => arabic ? ar : en;
  final dir = arabic ? pw.TextDirection.rtl : pw.TextDirection.ltr;

  final doc = pw.Document(title: 'AsthmaGuard report', author: 'AsthmaGuard');
  pw.Widget kv(String k, String v) => pw.Padding(
        padding: const pw.EdgeInsets.symmetric(vertical: 2),
        child: pw.Row(children: [
          pw.SizedBox(width: 150, child: pw.Text(k, style: pw.TextStyle(font: bold))),
          pw.Expanded(child: pw.Text(v)),
        ]),
      );
  final doseMax = deviceConfig['max_doses'];
  final doseInt = deviceConfig['dose_interval_s'];
  final doseText = (doseMax is num && doseMax > 0 && doseInt is num && doseInt > 0)
      ? t('$doseMax بخات للنوبة، بين كل بختين ${(doseInt / 3600).toStringAsFixed(1)} ساعة',
          '$doseMax puffs per episode, ${(doseInt / 3600).toStringAsFixed(1)} h apart')
      : t('غير محددة (البخ التلقائي متوقف)', 'not set (dosing disabled)');

  doc.addPage(pw.MultiPage(
    pageFormat: PdfPageFormat.a4,
    textDirection: dir,
    theme: pw.ThemeData.withFont(base: regular, bold: bold),
    header: (ctx) => pw.Row(mainAxisAlignment: pw.MainAxisAlignment.spaceBetween, children: [
      pw.Text(t('تقرير أزما جارد', 'AsthmaGuard report'), style: pw.TextStyle(font: bold, fontSize: 18, color: PdfColor.fromInt(0xFF0E7C86))),
      pw.Text('${dd.format(from)} → ${dd.format(to.subtract(const Duration(days: 1)))}', textDirection: pw.TextDirection.ltr),
    ]),
    footer: (ctx) => pw.Column(children: [
      pw.Divider(),
      pw.Text(
          t('نموذج أولي لمشروع طلابي وليس جهازًا طبيًا معتمدًا. أُنشئ ${df.format(now!)} — صفحة ${ctx.pageNumber}/${ctx.pagesCount}',
              'Student prototype, not a certified medical device. Generated ${df.format(now)} — page ${ctx.pageNumber}/${ctx.pagesCount}'),
          style: const pw.TextStyle(fontSize: 8, color: PdfColors.grey700)),
    ]),
    build: (ctx) => [
      pw.SizedBox(height: 8),
      kv(t('المريض', 'Patient'), '${patient.name} (${patient.age})'),
      kv(t('الطبيب', 'Doctor'), patient.doctorName.isEmpty ? '—' : patient.doctorName),
      kv(t('المعدل الأساسي', 'Baseline'),
          '${t('نبض', 'HR')} ${deviceConfig['baseline_hr'] ?? patient.baselineHr ?? '—'}, ${t('أكسجين', 'SpO2')} ${deviceConfig['baseline_spo2'] ?? patient.baselineSpo2 ?? '—'}%'
          '${patient.calibratedAt == null ? t(' (افتراضي)', ' (default)') : ' (${DateFormat.yMMMd(lc).format(patient.calibratedAt!)})'}'),
      kv(t('حدود الجرعات', 'Dose limits'), doseText),
      pw.SizedBox(height: 12),
      pw.Text(t('الملخص', 'Summary'), style: pw.TextStyle(font: bold, fontSize: 14)),
      pw.SizedBox(height: 4),
      kv(t('عدد النوبات', 'Episodes'), '${s.episodes}'),
      kv(t('إنذارات كاذبة', 'False alarms'), '${s.falseAlarms}'),
      kv(t('مرات إرسال الطوارئ', 'Emergencies sent'), '${s.emergencies}'),
      kv(t('مجموع البخات', 'Total puffs'), '${s.doses}'),
      kv(t('أقل أكسجين', 'Lowest SpO2'), s.minSpo2 == null ? '—' : '${s.minSpo2}%'),
      kv(t('أعلى نبض', 'Highest HR'), s.maxHr == null ? '—' : '${s.maxHr}'),
      kv(t('متوسط مدة النوبة', 'Average duration'), _dur(s.avgDuration)),
      pw.SizedBox(height: 12),
      pw.Text(t('النوبات', 'Episodes'), style: pw.TextStyle(font: bold, fontSize: 14)),
      pw.SizedBox(height: 4),
      if (eps.isEmpty)
        pw.Text(t('لا توجد نوبات في هذه الفترة', 'No episodes in this period'))
      else
        pw.TableHelper.fromTextArray(
          headerStyle: pw.TextStyle(font: bold, fontSize: 9),
          cellStyle: const pw.TextStyle(fontSize: 9),
          headerDecoration: const pw.BoxDecoration(color: PdfColors.grey200),
          cellAlignment: pw.Alignment.center,
          headers: [
            t('البداية', 'Start'),
            t('المدة', 'Duration'),
            t('بخات', 'Puffs'),
            t('أقل أكسجين', 'Min SpO2'),
            t('أعلى نبض', 'Max HR'),
            t('كحة/د', 'Cough/min'),
            t('طوارئ', 'Emergency'),
            t('كاذب', 'False'),
          ],
          data: [
            for (final e in eps)
              [
                df.format(e.start),
                e.open ? t('مستمرة', 'ongoing') : _dur(e.duration(now!)),
                '${e.doses}',
                e.minSpo2 == null ? '—' : '${e.minSpo2}%',
                e.maxHr?.toString() ?? '—',
                '${e.coughs}',
                e.emergencySent ? t('نعم', 'yes') : '',
                e.falseAlarm ? t('نعم', 'yes') : '',
              ],
          ],
        ),
    ],
  ));
  return doc.save();
}
