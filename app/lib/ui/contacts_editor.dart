import 'package:flutter/material.dart';

import '../data/models.dart';
import '../l10n.dart';

final _phoneRe = RegExp(r'^\+?[0-9]{8,15}$');

String normalizePhone(String p) => p.replaceAll(RegExp(r'[\s\-()]'), '');
bool validPhone(String p) => _phoneRe.hasMatch(normalizePhone(p));

class ContactsEditorController extends ChangeNotifier {
  final List<(TextEditingController, TextEditingController)> rows = [];
  ContactsEditorController(List<Contact> initial) {
    for (final c in initial) {
      rows.add((TextEditingController(text: c.name), TextEditingController(text: c.phone)));
    }
    if (rows.isEmpty) add();
  }
  void add() {
    rows.add((TextEditingController(), TextEditingController()));
    notifyListeners();
  }

  void remove(int i) {
    rows.removeAt(i);
    if (rows.isEmpty) add();
    notifyListeners();
  }

  List<Contact> valid() => [
        for (final r in rows)
          if (r.$1.text.trim().isNotEmpty && validPhone(r.$2.text))
            Contact(name: r.$1.text.trim(), phone: normalizePhone(r.$2.text)),
      ];
}

class ContactsEditor extends StatelessWidget {
  final ContactsEditorController controller;
  const ContactsEditor({super.key, required this.controller});

  @override
  Widget build(BuildContext context) {
    final l = L.of(context);
    return ListenableBuilder(
      listenable: controller,
      builder: (context, _) => Column(children: [
        for (var i = 0; i < controller.rows.length; i++)
          Padding(
            padding: const EdgeInsets.only(top: 8),
            child: Row(children: [
              Expanded(
                flex: 4,
                child: TextFormField(
                  controller: controller.rows[i].$1,
                  decoration: InputDecoration(labelText: l.t('contact_name'), border: const OutlineInputBorder()),
                ),
              ),
              const SizedBox(width: 8),
              Expanded(
                flex: 5,
                child: TextFormField(
                  controller: controller.rows[i].$2,
                  keyboardType: TextInputType.phone,
                  textDirection: TextDirection.ltr,
                  decoration: InputDecoration(labelText: l.t('contact_phone'), border: const OutlineInputBorder()),
                  validator: (v) => (v == null || v.isEmpty || validPhone(v)) ? null : l.t('bad_phone'),
                ),
              ),
              IconButton(
                tooltip: l.t('delete'),
                icon: const Icon(Icons.close),
                onPressed: () => controller.remove(i),
              ),
            ]),
          ),
        Align(
          alignment: AlignmentDirectional.centerStart,
          child: TextButton.icon(icon: const Icon(Icons.person_add), label: Text(l.t('add')), onPressed: controller.add),
        ),
      ]),
    );
  }
}
