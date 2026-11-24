// BLE link to the AsthmaGuard wristband (flutter_blue_plus).
// Connects, subscribes to live/event/cmd, sends the time on every connect,
// reads/writes config and reconnects automatically.
import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';

import '../core/protocol.dart';

enum LinkState { disconnected, connecting, connected }

class DeviceService {
  final _live = StreamController<LiveData>.broadcast();
  final _events = StreamController<DeviceEvent>.broadcast();
  final _replies = StreamController<String>.broadcast();
  final _link = StreamController<LinkState>.broadcast();
  final _config = StreamController<Map<String, dynamic>>.broadcast();

  Stream<LiveData> get live => _live.stream;
  Stream<DeviceEvent> get events => _events.stream;
  Stream<String> get replies => _replies.stream;
  Stream<LinkState> get link => _link.stream;
  Stream<Map<String, dynamic>> get config => _config.stream;

  LinkState state = LinkState.disconnected;
  Map<String, dynamic> lastConfig = {};
  String? remoteId;

  BluetoothDevice? _dev;
  BluetoothCharacteristic? _cLive, _cEvent, _cConfig, _cTime, _cCmd;
  final List<StreamSubscription> _subs = [];
  StreamSubscription<BluetoothConnectionState>? _connSub;
  Timer? _retry;
  bool _wanted = false;

  // ------------------------------------------------------------------ scan
  Stream<List<ScanResult>> get scanResults => FlutterBluePlus.scanResults;

  Future<void> startScan() async {
    await FlutterBluePlus.adapterState.where((s) => s == BluetoothAdapterState.on).first.timeout(
          const Duration(seconds: 10),
          onTimeout: () => BluetoothAdapterState.off,
        );
    await FlutterBluePlus.startScan(
      withServices: [Guid(AgUuids.service)],
      timeout: const Duration(seconds: 12),
    );
  }

  Future<void> stopScan() => FlutterBluePlus.stopScan();

  // ------------------------------------------------------------------ connect
  Future<void> connectTo(String id) async {
    remoteId = id;
    _wanted = true;
    await _connect();
  }

  Future<void> forget() async {
    _wanted = false;
    _retry?.cancel();
    await _connSub?.cancel();
    _connSub = null;
    try {
      await _dev?.disconnect();
    } catch (_) {}
    _dev = null;
    remoteId = null;
    _setState(LinkState.disconnected);
  }

  void _setState(LinkState s) {
    state = s;
    _link.add(s);
  }

  void _scheduleRetry() {
    _retry?.cancel();
    if (!_wanted) return;
    _retry = Timer(const Duration(seconds: 10), _connect);
  }

  Future<void> _connect() async {
    final id = remoteId;
    if (id == null || state == LinkState.connecting) return;
    _setState(LinkState.connecting);
    final dev = BluetoothDevice.fromId(id);
    _dev = dev;
    await _connSub?.cancel();
    _connSub = dev.connectionState.listen((s) {
      if (s == BluetoothConnectionState.disconnected && state == LinkState.connected) {
        _clearChars();
        _setState(LinkState.disconnected);
        _scheduleRetry();
      }
    });
    try {
      // Student project: FlutterBluePlus nonprofit license (educational use).
      await dev.connect(license: License.nonprofit, timeout: const Duration(seconds: 15), mtu: 247);
      await _setup(dev);
      _setState(LinkState.connected);
    } catch (e) {
      debugPrint('connect failed: $e');
      _clearChars();
      try {
        await dev.disconnect();
      } catch (_) {}
      _setState(LinkState.disconnected);
      _scheduleRetry();
    }
  }

  void _clearChars() {
    for (final s in _subs) {
      s.cancel();
    }
    _subs.clear();
    _cLive = _cEvent = _cConfig = _cTime = _cCmd = null;
  }

  Future<void> _setup(BluetoothDevice dev) async {
    final services = await dev.discoverServices();
    final svc = services.firstWhere((s) => s.uuid == Guid(AgUuids.service),
        orElse: () => throw StateError('AsthmaGuard service not found'));
    BluetoothCharacteristic ch(String u) =>
        svc.characteristics.firstWhere((c) => c.uuid == Guid(u), orElse: () => throw StateError('missing $u'));
    _cLive = ch(AgUuids.live);
    _cEvent = ch(AgUuids.event);
    _cConfig = ch(AgUuids.config);
    _cTime = ch(AgUuids.time);
    _cCmd = ch(AgUuids.cmd);

    _subs.add(_cLive!.onValueReceived.listen((v) {
      final l = LiveData.tryParse(utf8.decode(v, allowMalformed: true), DateTime.now());
      if (l != null) _live.add(l);
    }));
    _subs.add(_cEvent!.onValueReceived.listen((v) {
      final e = DeviceEvent.tryParse(utf8.decode(v, allowMalformed: true), DateTime.now());
      if (e != null) _events.add(e);
    }));
    _subs.add(_cCmd!.onValueReceived.listen((v) => _replies.add(utf8.decode(v, allowMalformed: true))));
    await _cLive!.setNotifyValue(true);
    await _cCmd!.setNotifyValue(true);
    // The phone sends the time on every connect (no RTC on the Pico).
    await sendTime();
    await readConfig();
    // Subscribing to events last: pending events start flowing now.
    await _cEvent!.setNotifyValue(true);
  }

  // ------------------------------------------------------------------ I/O
  Future<void> sendTime() async {
    final t = DateTime.now().millisecondsSinceEpoch ~/ 1000;
    await _cTime?.write(utf8.encode('$t'), withoutResponse: false);
  }

  Future<Map<String, dynamic>> readConfig() async {
    final c = _cConfig;
    if (c == null) return lastConfig;
    final v = await c.read();
    try {
      lastConfig = (jsonDecode(utf8.decode(v)) as Map).cast<String, dynamic>();
      _config.add(lastConfig);
    } catch (_) {}
    return lastConfig;
  }

  /// Writes config changes (partial JSON, chunked). Returns the device's
  /// replies ("OK config" / "ERR ...") for each chunk.
  Future<List<String>> writeConfig(Map<String, num> changes) async {
    final c = _cConfig;
    if (c == null) throw StateError('not connected');
    final out = <String>[];
    for (final part in chunkConfig(changes)) {
      final reply = replies.first.timeout(const Duration(seconds: 5), onTimeout: () => 'ERR timeout');
      await c.write(utf8.encode(part), withoutResponse: false);
      out.add(await reply);
    }
    await readConfig();
    return out;
  }

  Future<String> command(String cmd, {bool expectReply = true}) async {
    final c = _cCmd;
    if (c == null) throw StateError('not connected');
    final reply = expectReply
        ? replies.first.timeout(const Duration(seconds: 5), onTimeout: () => 'ERR timeout')
        : Future.value('');
    await c.write(utf8.encode(cmd), withoutResponse: false);
    return reply;
  }

  Future<void> ack(int eventId) async {
    if (_cCmd == null) return;
    try {
      await command('ACK:$eventId', expectReply: false);
    } catch (e) {
      debugPrint('ack $eventId failed: $e');
    }
  }
}
