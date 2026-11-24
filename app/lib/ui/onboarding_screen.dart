import 'package:flutter/material.dart';

import '../app_controller.dart';
import '../data/models.dart';
import '../l10n.dart';
import 'contacts_editor.dart';
import 'scope.dart';

class OnboardingScreen extends StatefulWidget {
  const OnboardingScreen({super.key});
  @override
  State<OnboardingScreen> createState() => _OnboardingScreenState();
}

class _OnboardingScreenState extends State<OnboardingScreen> {
  final _form = GlobalKey<FormState>();
  final _name = TextEditingController();
  final _age = TextEditingController();
  final _doctor = TextEditingController();
  final _contacts = ContactsEditorController([const Contact(name: '', phone: '')]);
  Map<String, bool>? _perms;
  bool _saving = false;

  @override
  Widget build(BuildContext context) {
    final l = L.of(context);
    final c = AppScope.of(context);
    final theme = Theme.of(context);
    return Scaffold(
      appBar: AppBar(
        title: Text(l.t('app')),
        actions: [
          TextButton(
            onPressed: () => c.setLocale(l.ar ? 'en' : 'ar'),
            child: Text(l.ar ? 'English' : 'العربية'),
          ),
        ],
      ),
      body: Form(
        key: _form,
        child: ListView(
          padding: const EdgeInsets.all(16),
          children: [
            Text(l.t('welcome'), style: theme.textTheme.headlineSmall),
            const SizedBox(height: 8),
            Text(l.t('welcome_sub'), style: theme.textTheme.bodyLarge),
            const SizedBox(height: 12),
            Card(
              color: theme.colorScheme.tertiaryContainer,
              child: Padding(
                padding: const EdgeInsets.all(12),
                child: Row(children: [
                  const Icon(Icons.info_outline),
                  const SizedBox(width: 8),
                  Expanded(child: Text(l.t('disclaimer'))),
                ]),
              ),
            ),
            const SizedBox(height: 16),
            TextFormField(
              controller: _name,
              decoration: InputDecoration(labelText: l.t('patient_name'), border: const OutlineInputBorder()),
              validator: (v) => (v == null || v.trim().isEmpty) ? l.t('required') : null,
            ),
            const SizedBox(height: 12),
            TextFormField(
              controller: _age,
              keyboardType: TextInputType.number,
              decoration: InputDecoration(
                  labelText: l.t('age'), helperText: l.t('age_adult'), border: const OutlineInputBorder()),
              validator: (v) {
                final a = int.tryParse(v ?? '');
                if (a == null) return l.t('required');
                if (a < 18 || a > 120) return l.t('age_adult');
                return null;
              },
            ),
            const SizedBox(height: 12),
            TextFormField(
              controller: _doctor,
              decoration: InputDecoration(labelText: l.t('doctor_name'), border: const OutlineInputBorder()),
            ),
            const SizedBox(height: 20),
            Text(l.t('contacts'), style: theme.textTheme.titleMedium),
            Text(l.t('contacts_sub'), style: theme.textTheme.bodySmall),
            ContactsEditor(controller: _contacts),
            const SizedBox(height: 20),
            Text(l.t('permissions'), style: theme.textTheme.titleMedium),
            Text(l.t('permissions_sub'), style: theme.textTheme.bodySmall),
            const SizedBox(height: 8),
            Align(
              alignment: AlignmentDirectional.centerStart,
              child: OutlinedButton.icon(
                icon: Icon(_perms == null ? Icons.lock_open : Icons.check),
                label: Text(l.t('grant')),
                onPressed: () async {
                  final r = await c.platform.requestPermissions();
                  setState(() => _perms = r);
                },
              ),
            ),
            const SizedBox(height: 24),
            FilledButton(
              onPressed: _saving ? null : () => _save(c),
              child: Padding(padding: const EdgeInsets.all(12), child: Text(l.t('next'))),
            ),
          ],
        ),
      ),
    );
  }

  Future<void> _save(AppController c) async {
    if (!_form.currentState!.validate()) return;
    final contacts = _contacts.valid();
    if (contacts.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(L.of(context).t('contacts_sub'))));
      return;
    }
    setState(() => _saving = true);
    if (_perms == null) await c.platform.requestPermissions();
    await c.savePatient(
      Patient(
        name: _name.text.trim(),
        age: int.parse(_age.text),
        doctorName: _doctor.text.trim(),
        createdAt: DateTime.now(),
      ),
      contacts,
    );
  }
}
