import 'package:flutter/material.dart';

import '../l10n.dart';
import '../services/device_service.dart';
import 'contacts_editor.dart';
import 'doctor_screen.dart';
import 'pair_screen.dart';
import 'scope.dart';
import 'simulation_screen.dart';

class SettingsScreen extends StatefulWidget {
  const SettingsScreen({super.key});
  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  int _taps = 0; // 5 taps on the version line opens the hidden simulation screen

  void _snack(String s) => ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(s)));

  Future<void> _cmd(String cmd) async {
    final c = AppScope.read(context);
    final l = L.of(context);
    if (c.link != LinkState.connected) return _snack(l.t('need_connection'));
    try {
      _snack(await c.device.command(cmd));
    } catch (e) {
      _snack('${l.t('error')}: $e');
    }
  }

  @override
  Widget build(BuildContext context) {
    final c = AppScope.of(context);
    final l = L.of(context);
    return ListView(children: [
      ListTile(
        leading: const Icon(Icons.contacts),
        title: Text(l.t('contacts')),
        subtitle: Text(c.contacts.map((x) => x.name).join('، ')),
        onTap: () => Navigator.push(context, MaterialPageRoute(builder: (_) => const _ContactsPage())),
      ),
      ListTile(
        leading: const Icon(Icons.notifications_active),
        title: Text(l.t('test_alert')),
        onTap: () => _cmd('TEST_ALERT'),
      ),
      ListTile(
        leading: const Icon(Icons.precision_manufacturing),
        title: Text(l.t('test_servo')),
        subtitle: Text(l.t('test_servo_warn')),
        onTap: () async {
          final ok = await showDialog<bool>(
            context: context,
            builder: (d) => AlertDialog(
              title: Text(l.t('test_servo')),
              content: Text(l.t('test_servo_warn')),
              actions: [
                TextButton(onPressed: () => Navigator.pop(d, false), child: Text(l.t('cancel'))),
                FilledButton(onPressed: () => Navigator.pop(d, true), child: Text(l.t('ok'))),
              ],
            ),
          );
          if (ok == true) await _cmd('TEST_SERVO');
        },
      ),
      ListTile(
        leading: const Icon(Icons.bluetooth),
        title: Text(l.t('re_pair')),
        subtitle: Text(c.pairedId ?? '—', textDirection: TextDirection.ltr),
        onTap: () async {
          await c.unpair();
          if (!context.mounted) return;
          Navigator.push(context, MaterialPageRoute(builder: (_) => const PairScreen()));
        },
      ),
      ListTile(
        leading: const Icon(Icons.language),
        title: Text(l.t('language')),
        trailing: SegmentedButton<String>(
          segments: const [ButtonSegment(value: 'ar', label: Text('ع')), ButtonSegment(value: 'en', label: Text('EN'))],
          selected: {c.locale.languageCode},
          onSelectionChanged: (s) => c.setLocale(s.first),
        ),
      ),
      const Divider(),
      ListTile(
        leading: const Icon(Icons.medical_services),
        title: Text(l.t('doctor')),
        onTap: () => Navigator.push(context, MaterialPageRoute(builder: (_) => const DoctorScreen())),
      ),
      const Divider(),
      ListTile(
        title: Text(l.t('disclaimer'), style: Theme.of(context).textTheme.bodySmall),
        subtitle: const Text('AsthmaGuard 0.1.0'),
        onTap: () {
          if (++_taps >= 5) {
            _taps = 0;
            Navigator.push(context, MaterialPageRoute(builder: (_) => const SimulationScreen()));
          }
        },
      ),
    ]);
  }
}

class _ContactsPage extends StatefulWidget {
  const _ContactsPage();
  @override
  State<_ContactsPage> createState() => _ContactsPageState();
}

class _ContactsPageState extends State<_ContactsPage> {
  late final ContactsEditorController _ctl = ContactsEditorController(AppScope.read(context).contacts);

  @override
  Widget build(BuildContext context) {
    final l = L.of(context);
    return Scaffold(
      appBar: AppBar(title: Text(l.t('contacts'))),
      body: ListView(padding: const EdgeInsets.all(16), children: [
        Text(l.t('contacts_sub')),
        ContactsEditor(controller: _ctl),
        const SizedBox(height: 16),
        FilledButton(
          onPressed: () async {
            final c = AppScope.read(context);
            final list = _ctl.valid();
            if (list.isEmpty || c.patient == null) {
              ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(l.t('contacts_sub'))));
              return;
            }
            await c.savePatient(c.patient!, list);
            if (context.mounted) Navigator.pop(context);
          },
          child: Text(l.t('save')),
        ),
      ]),
    );
  }
}
