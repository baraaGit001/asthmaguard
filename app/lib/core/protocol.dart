// BLE protocol of the AsthmaGuard device (docs/PLAN.md section 5.3,
// firmware/src/ble.h). Pure Dart so it can be unit-tested.
import 'dart:convert';

class AgUuids {
  static const service = 'a5f10000-7c1e-4b9a-9d3e-6a1f00000000';
  static const live = 'a5f10001-7c1e-4b9a-9d3e-6a1f00000000';
  static const event = 'a5f10002-7c1e-4b9a-9d3e-6a1f00000000';
  static const config = 'a5f10003-7c1e-4b9a-9d3e-6a1f00000000';
  static const time = 'a5f10004-7c1e-4b9a-9d3e-6a1f00000000';
  static const cmd = 'a5f10005-7c1e-4b9a-9d3e-6a1f00000000';
}

/// Device states, ordered by severity (used for episode.state_max).
const deviceStates = [
  'NORMAL',
  'SUSPECT',
  'RESOLVED',
  'MONITOR',
  'DOSE',
  'WAIT_CONFIRM',
  'WAIT_RAISE',
  'ALERT',
  'EMERGENCY',
];

int stateRank(String s) {
  final i = deviceStates.indexOf(s);
  return i < 0 ? 0 : i;
}

/// States that mean an attack episode is going on.
bool isEpisodeState(String s) =>
    const {'ALERT', 'WAIT_RAISE', 'WAIT_CONFIRM', 'DOSE', 'MONITOR', 'EMERGENCY'}.contains(s);

class LiveData {
  final int hr;
  final int spo2;
  final bool valid;
  final int cough60;
  final String activity;
  final String state;
  final int battery;
  final bool sim;
  final int pending;
  final bool doseEnabled;
  final DateTime at;

  const LiveData({
    required this.hr,
    required this.spo2,
    required this.valid,
    required this.cough60,
    required this.activity,
    required this.state,
    required this.battery,
    required this.sim,
    required this.pending,
    required this.doseEnabled,
    required this.at,
  });

  static LiveData? tryParse(String text, DateTime at) {
    try {
      final j = jsonDecode(text);
      if (j is! Map<String, dynamic> || !j.containsKey('st')) return null;
      int i(String k) => (j[k] as num?)?.round() ?? 0;
      return LiveData(
        hr: i('hr'),
        spo2: i('spo2'),
        valid: i('v') == 1,
        cough60: i('c60'),
        activity: (j['act'] as String?) ?? 'REST',
        state: (j['st'] as String?) ?? 'NORMAL',
        battery: i('bat'),
        sim: i('sim') == 1,
        pending: i('pend'),
        doseEnabled: i('dose') == 1,
        at: at,
      );
    } catch (_) {
      return null;
    }
  }
}

class DeviceEvent {
  final int id;
  final int epoch; // 0 when the device clock was not set yet
  final int uptime;
  final String type;
  final Map<String, dynamic> data;
  final DateTime receivedAt;

  const DeviceEvent({
    required this.id,
    required this.epoch,
    required this.uptime,
    required this.type,
    required this.data,
    required this.receivedAt,
  });

  /// Best estimate of when the event happened on the device.
  DateTime get time => epoch > 1600000000
      ? DateTime.fromMillisecondsSinceEpoch(epoch * 1000)
      : receivedAt;

  bool get isEmergency => type == 'EMERGENCY' || type == 'MANUAL_SOS';

  int? get hr => (data['hr'] as num?)?.round();
  int? get spo2 => (data['spo2'] as num?)?.round();
  bool get validReading => data['v'] == 1;

  static DeviceEvent? tryParse(String text, DateTime receivedAt) {
    try {
      final j = jsonDecode(text);
      if (j is! Map<String, dynamic> || j['id'] == null || j['type'] == null) return null;
      return DeviceEvent(
        id: (j['id'] as num).toInt(),
        epoch: (j['t'] as num?)?.toInt() ?? 0,
        uptime: (j['up'] as num?)?.toInt() ?? 0,
        type: j['type'] as String,
        data: (j['data'] as Map?)?.cast<String, dynamic>() ?? const {},
        receivedAt: receivedAt,
      );
    } catch (_) {
      return null;
    }
  }

  String toJson() => jsonEncode({'id': id, 't': epoch, 'up': uptime, 'type': type, 'data': data});
}

/// Splits a config change into JSON writes that fit one BLE write
/// (the firmware accepts partial updates; each must be < [maxBytes]).
List<String> chunkConfig(Map<String, num> changes, {int maxBytes = 180}) {
  final out = <String>[];
  var cur = <String, num>{};
  for (final e in changes.entries) {
    final next = {...cur, e.key: e.value};
    if (cur.isNotEmpty && utf8.encode(jsonEncode(next)).length > maxBytes) {
      out.add(jsonEncode(cur));
      cur = {e.key: e.value};
    } else {
      cur = next;
    }
  }
  if (cur.isNotEmpty) out.add(jsonEncode(cur));
  return out;
}
