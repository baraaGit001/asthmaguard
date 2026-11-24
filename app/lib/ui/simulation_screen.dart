// Hidden simulation screen (section 5.4): buttons for the SIM: commands so
// every scenario of section 8 can be demoed without a real attack.
import 'package:flutter/material.dart';

import '../l10n.dart';
import 'scope.dart';

class SimulationScreen extends StatefulWidget {
  const SimulationScreen({super.key});
  @override
  State<SimulationScreen> createState() => _SimulationScreenState();
}

class _SimulationScreenState extends State<SimulationScreen> {
  final List<String> _log = [];

  Future<void> _send(List<String> cmds) async {
    final c = AppScope.read(context);
    for (final cmd in cmds) {
      String r;
      try {
        r = await c.device.command(cmd);
      } catch (e) {
        r = 'ERR $e';
      }
      setState(() => _log.insert(0, '$cmd → $r'));
    }
  }

  Widget _btn(String label, List<String> cmds, {Color? color}) => Padding(
        padding: const EdgeInsets.all(4),
        child: FilledButton.tonal(
          style: color == null ? null : FilledButton.styleFrom(backgroundColor: color.withValues(alpha: 0.2)),
          onPressed: () => _send(cmds),
          child: Text(label),
        ),
      );

  @override
  Widget build(BuildContext context) {
    final l = L.of(context);
    final live = AppScope.of(context).live;
    return Scaffold(
      appBar: AppBar(title: Text(l.t('simulation'))),
      body: ListView(padding: const EdgeInsets.all(12), children: [
        Text(l.t('simulation_sub')),
        if (live != null)
          Padding(
            padding: const EdgeInsets.symmetric(vertical: 8),
            child: Text('st=${live.state}  hr=${live.hr}  spo2=${live.spo2}  v=${live.valid}  c60=${live.cough60}  act=${live.activity}',
                textDirection: TextDirection.ltr, style: const TextStyle(fontFamily: 'monospace')),
          ),
        const Divider(),
        Wrap(children: [
          _btn('SIM ON', ['SIM:ON']),
          _btn('SIM OFF', ['SIM:OFF']),
          _btn('clear', ['SIM:clear']),
        ]),
        const Text('Scenarios'),
        Wrap(children: [
          _btn('1 rest', ['SIM:clear', 'SIM:hr=72', 'SIM:spo2=97', 'SIM:cough=0', 'SIM:act=REST']),
          _btn('2 jogging', ['SIM:clear', 'SIM:hr=150', 'SIM:act=EXERCISE', 'SIM:cough=0']),
          _btn('3 cough only', ['SIM:clear', 'SIM:cough=6', 'SIM:hr=78', 'SIM:spo2=97', 'SIM:act=REST']),
          _btn('4 cough + HR 125', ['SIM:clear', 'SIM:cough=6', 'SIM:hr=125', 'SIM:act=REST'], color: Colors.orange),
          _btn('9 SpO2 91', ['SIM:clear', 'SIM:spo2=91', 'SIM:act=REST'], color: Colors.red),
          _btn('improve', ['SIM:cough=0', 'SIM:hr=78', 'SIM:spo2=97'], color: Colors.green),
          _btn('moving (invalid)', ['SIM:valid=0']),
          _btn('battery 10%', ['SIM:bat=10']),
        ]),
        const Text('Actions'),
        Wrap(children: [
          _btn('raise arm', ['SIM:raise']),
          _btn('button short', ['SIM:btn=short']),
          _btn('long 2 s (cancel)', ['SIM:btn=long']),
          _btn('SOS 5 s', ['SIM:btn=sos'], color: Colors.red),
        ]),
        const Divider(),
        for (final s in _log) Text(s, textDirection: TextDirection.ltr, style: const TextStyle(fontFamily: 'monospace', fontSize: 12)),
      ]),
    );
  }
}
