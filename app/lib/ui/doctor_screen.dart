import 'package:flutter/material.dart';
import 'package:flutter_secure_storage/flutter_secure_storage.dart';
import 'package:intl/intl.dart';
import 'package:printing/printing.dart';

import '../core/config_fields.dart';
import '../l10n.dart';
import '../report/pdf_report.dart';
import '../services/device_service.dart';
import 'scope.dart';

const _storage = FlutterSecureStorage();
const _pinKey = 'doctor_pin';

class DoctorScreen extends StatefulWidget {
  const DoctorScreen({super.key});
  @override
  State<DoctorScreen> createState() => _DoctorScreenState();
}

class _DoctorScreenState extends State<DoctorScreen> {
  bool _unlocked = false;
  String? _savedPin;
  bool _loading = true;
  final _pin = TextEditingController();
  String? _pinError;

  @override
  void initState() {
    super.initState();
    _storage.read(key: _pinKey).then((v) => setState(() {
          _savedPin = v;
          _loading = false;
        }));
  }

  Future<void> _submitPin() async {
    final l = L.of(context);
    final p = _pin.text.trim();
    if (_savedPin == null) {
      if (p.length < 4 || int.tryParse(p) == null) return setState(() => _pinError = l.t('pin_set'));
      await _storage.write(key: _pinKey, value: p);
      setState(() => _unlocked = true);
    } else if (p == _savedPin) {
      setState(() => _unlocked = true);
    } else {
      setState(() => _pinError = l.t('pin_wrong'));
    }
  }

  @override
  Widget build(BuildContext context) {
    final l = L.of(context);
    return Scaffold(
      appBar: AppBar(title: Text(l.t('doctor'))),
      body: _loading
          ? const Center(child: CircularProgressIndicator())
          : !_unlocked
              ? Center(
                  child: ConstrainedBox(
                    constraints: const BoxConstraints(maxWidth: 360),
                    child: Padding(
                      padding: const EdgeInsets.all(24),
                      child: Column(mainAxisSize: MainAxisSize.min, children: [
                        const Icon(Icons.lock, size: 48),
                        const SizedBox(height: 12),
                        Text(_savedPin == null ? l.t('pin_set') : l.t('pin_enter'), textAlign: TextAlign.center),
                        const SizedBox(height: 12),
                        TextField(
                          controller: _pin,
                          obscureText: true,
                          keyboardType: TextInputType.number,
                          textAlign: TextAlign.center,
                          autofocus: true,
                          decoration: InputDecoration(border: const OutlineInputBorder(), errorText: _pinError, labelText: l.t('pin')),
                          onSubmitted: (_) => _submitPin(),
                        ),
                        const SizedBox(height: 12),
                        FilledButton(onPressed: _submitPin, child: Text(l.t('ok'))),
                      ]),
                    ),
                  ),
                )
              : const _DoctorBody(),
    );
  }
}

class _DoctorBody extends StatefulWidget {
  const _DoctorBody();
  @override
  State<_DoctorBody> createState() => _DoctorBodyState();
}

class _DoctorBodyState extends State<_DoctorBody> {
  final Map<String, TextEditingController> _ctl = {};
  final _maxDoses = TextEditingController();
  final _intervalH = TextEditingController();
  bool _busy = false;
  DateTimeRange? _range;

  @override
  void initState() {
    super.initState();
    _fill(AppScope.read(context).device.lastConfig);
    final now = DateTime.now();
    _range = DateTimeRange(start: now.subtract(const Duration(days: 30)), end: now);
  }

  String _fmt(dynamic v) {
    if (v is num) return v == v.roundToDouble() ? v.toInt().toString() : v.toStringAsFixed(2);
    return '';
  }

  void _fill(Map<String, dynamic> cfg) {
    for (final g in configGroups) {
      for (final f in g.fields) {
        (_ctl[f.key] ??= TextEditingController()).text = _fmt(cfg[f.key]);
      }
    }
    _maxDoses.text = _fmt(cfg['max_doses']);
    final s = cfg['dose_interval_s'];
    _intervalH.text = s is num ? _fmt(s / 3600) : '';
  }

  void _snack(String s) => ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(s)));

  Future<void> _saveDose() async {
    final l = L.of(context);
    final m = num.tryParse(_maxDoses.text);
    final h = num.tryParse(_intervalH.text);
    final err = validateDoseLimits(m, h == null ? null : (h * 3600).round());
    if (err != null) return _snack(err == 'both' ? l.t('dose_both') : '${l.t('error')}: $err');
    await _write({'max_doses': m!.round(), 'dose_interval_s': (h! * 3600).round()});
  }

  Future<void> _saveThresholds() async {
    final l = L.of(context);
    final cfg = AppScope.read(context).device.lastConfig;
    final changes = <String, num>{};
    for (final g in configGroups) {
      for (final f in g.fields) {
        final t = _ctl[f.key]!.text.trim();
        if (t.isEmpty) continue;
        final v = num.tryParse(t);
        if (v == null || !f.valid(v)) return _snack('${l.ar ? f.ar : f.en}: ${f.min} – ${f.max}');
        if (cfg[f.key] != v) changes[f.key] = v;
      }
    }
    if (changes.isEmpty) return _snack(l.t('saved'));
    await _write(changes);
  }

  Future<void> _write(Map<String, num> changes) async {
    final l = L.of(context);
    final c = AppScope.read(context);
    if (c.link != LinkState.connected) return _snack(l.t('need_connection'));
    setState(() => _busy = true);
    try {
      final replies = await c.device.writeConfig(changes);
      final bad = replies.where((r) => !r.startsWith('OK')).toList();
      _fill(c.device.lastConfig);
      _snack(bad.isEmpty ? l.t('saved') : bad.join('\n'));
    } catch (e) {
      _snack('${l.t('error')}: $e');
    }
    if (mounted) setState(() => _busy = false);
  }

  Future<void> _exportPdf() async {
    final c = AppScope.read(context);
    final r = _range!;
    setState(() => _busy = true);
    try {
      final bytes = await buildReport(
        store: c.store,
        patient: c.patient!,
        from: DateTime(r.start.year, r.start.month, r.start.day),
        to: DateTime(r.end.year, r.end.month, r.end.day).add(const Duration(days: 1)),
        deviceConfig: c.device.lastConfig,
        arabic: L.of(context).ar,
      );
      await Printing.sharePdf(bytes: bytes, filename: 'asthmaguard-${DateFormat('yyyyMMdd').format(r.end)}.pdf');
    } catch (e) {
      if (mounted) _snack('${L.of(context).t('error')}: $e');
    }
    if (mounted) setState(() => _busy = false);
  }

  @override
  Widget build(BuildContext context) {
    final l = L.of(context);
    final c = AppScope.of(context);
    final theme = Theme.of(context);
    final p = c.patient;
    final lc = l.ar ? 'ar' : 'en';
    return Stack(children: [
      ListView(padding: const EdgeInsets.all(16), children: [
        if (c.link != LinkState.connected)
          Card(color: theme.colorScheme.errorContainer, child: ListTile(leading: const Icon(Icons.bluetooth_disabled), title: Text(l.t('need_connection')))),
        // ---- dose limits
        Text(l.t('dose_limits'), style: theme.textTheme.titleLarge),
        Text(l.t('dose_limits_sub'), style: theme.textTheme.bodySmall),
        const SizedBox(height: 8),
        Row(children: [
          Expanded(
            child: TextField(
              controller: _maxDoses,
              keyboardType: TextInputType.number,
              decoration: InputDecoration(labelText: doseFields[0].ar == '' ? '' : (l.ar ? doseFields[0].ar : doseFields[0].en), border: const OutlineInputBorder()),
            ),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: TextField(
              controller: _intervalH,
              keyboardType: const TextInputType.numberWithOptions(decimal: true),
              decoration: InputDecoration(labelText: '${l.ar ? doseFields[1].ar : doseFields[1].en} (${l.t('hours')})', border: const OutlineInputBorder()),
            ),
          ),
        ]),
        const SizedBox(height: 8),
        Align(alignment: AlignmentDirectional.centerEnd, child: FilledButton(onPressed: _busy ? null : _saveDose, child: Text(l.t('save')))),
        const Divider(height: 32),
        // ---- baselines
        Text(l.t('baselines'), style: theme.textTheme.titleLarge),
        ListTile(
          contentPadding: EdgeInsets.zero,
          title: Text('${l.t('hr')}: ${_fmt(c.device.lastConfig['baseline_hr'])}   ${l.t('spo2')}: ${_fmt(c.device.lastConfig['baseline_spo2'])}%'),
          subtitle: Text(p?.calibratedAt == null
              ? l.t('not_calibrated')
              : '${l.t('calibrated_at')}: ${DateFormat.yMMMd(lc).format(p!.calibratedAt!)}'),
        ),
        const Divider(height: 32),
        // ---- thresholds
        Text(l.t('thresholds'), style: theme.textTheme.titleLarge),
        for (final g in configGroups)
          ExpansionTile(
            tilePadding: EdgeInsets.zero,
            title: Text(l.ar ? g.ar : g.en),
            children: [
              for (final f in g.fields)
                Padding(
                  padding: const EdgeInsets.only(bottom: 8),
                  child: TextField(
                    controller: _ctl[f.key],
                    keyboardType: const TextInputType.numberWithOptions(decimal: true),
                    decoration: InputDecoration(
                      labelText: l.ar ? f.ar : f.en,
                      helperText: '${f.min} – ${f.max} ${f.unit}',
                      border: const OutlineInputBorder(),
                    ),
                  ),
                ),
            ],
          ),
        const SizedBox(height: 8),
        Align(alignment: AlignmentDirectional.centerEnd, child: FilledButton(onPressed: _busy ? null : _saveThresholds, child: Text(l.t('save')))),
        const Divider(height: 32),
        // ---- PDF
        Text(l.t('export_pdf'), style: theme.textTheme.titleLarge),
        ListTile(
          contentPadding: EdgeInsets.zero,
          leading: const Icon(Icons.date_range),
          title: Text('${DateFormat.yMMMd(lc).format(_range!.start)} – ${DateFormat.yMMMd(lc).format(_range!.end)}'),
          onTap: () async {
            final r = await showDateRangePicker(
              context: context,
              firstDate: DateTime(2025),
              lastDate: DateTime.now(),
              initialDateRange: _range,
            );
            if (r != null) setState(() => _range = r);
          },
        ),
        FilledButton.icon(
          icon: const Icon(Icons.picture_as_pdf),
          label: Text(l.t('export_pdf')),
          onPressed: _busy || p == null ? null : _exportPdf,
        ),
        const SizedBox(height: 32),
      ]),
      if (_busy) const Positioned(top: 0, left: 0, right: 0, child: LinearProgressIndicator()),
    ]);
  }
}
