import 'dart:async';

import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import '../l10n.dart';
import '../services/device_service.dart';
import 'scope.dart';

class PairScreen extends StatefulWidget {
  final bool first;
  const PairScreen({super.key, this.first = false});
  @override
  State<PairScreen> createState() => _PairScreenState();
}

class _PairScreenState extends State<PairScreen> {
  List<ScanResult> _results = [];
  StreamSubscription? _sub;
  bool _scanning = false;
  String? _connectingId;
  String? _error;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) => _scan());
  }

  @override
  void dispose() {
    _sub?.cancel();
    AppScope.read(context).device.stopScan();
    super.dispose();
  }

  Future<void> _scan() async {
    final c = AppScope.read(context);
    setState(() {
      _scanning = true;
      _error = null;
      _results = [];
    });
    _sub?.cancel();
    _sub = c.device.scanResults.listen((r) => setState(() => _results = r));
    try {
      await c.device.startScan();
      await FlutterBluePlus.isScanning.where((s) => !s).first;
    } catch (e) {
      _error = '$e';
    }
    if (mounted) setState(() => _scanning = false);
  }

  Future<void> _connect(ScanResult r) async {
    final c = AppScope.read(context);
    setState(() => _connectingId = r.device.remoteId.str);
    await c.device.stopScan();
    await c.connect(r.device.remoteId.str);
    final ok = await c.device.link
        .firstWhere((s) => s != LinkState.connecting)
        .timeout(const Duration(seconds: 30), onTimeout: () => LinkState.disconnected);
    if (!mounted) return;
    setState(() => _connectingId = null);
    if (ok == LinkState.connected || c.device.state == LinkState.connected) {
      if (!widget.first && Navigator.canPop(context)) Navigator.pop(context);
    } else {
      setState(() => _error = L.of(context).t('disconnected'));
    }
  }

  @override
  Widget build(BuildContext context) {
    final l = L.of(context);
    return Scaffold(
      appBar: AppBar(title: Text(l.t('pair'))),
      body: Column(children: [
        if (_scanning) const LinearProgressIndicator(),
        if (_error != null)
          Padding(padding: const EdgeInsets.all(12), child: Text(_error!, style: const TextStyle(color: Colors.red))),
        Expanded(
          child: _results.isEmpty
              ? Center(child: Text(_scanning ? l.t('scanning') : l.t('no_devices'), textAlign: TextAlign.center))
              : ListView(children: [
                  for (final r in _results)
                    ListTile(
                      leading: const Icon(Icons.watch),
                      title: Text(r.device.platformName.isNotEmpty ? r.device.platformName : 'AsthmaGuard'),
                      subtitle: Text('${r.device.remoteId.str}  •  ${r.rssi} dBm', textDirection: TextDirection.ltr),
                      trailing: _connectingId == r.device.remoteId.str
                          ? const SizedBox(width: 24, height: 24, child: CircularProgressIndicator(strokeWidth: 2))
                          : const Icon(Icons.link),
                      onTap: _connectingId == null ? () => _connect(r) : null,
                    ),
                ]),
        ),
        Padding(
          padding: const EdgeInsets.all(16),
          child: FilledButton.icon(
            icon: const Icon(Icons.bluetooth_searching),
            label: Text(l.t('scan')),
            onPressed: _scanning ? null : _scan,
          ),
        ),
      ]),
    );
  }
}
