// Data model (docs/PLAN.md section 6.3).

class Patient {
  final String name;
  final int age;
  final String doctorName;
  final double? baselineHr;
  final double? baselineSpo2;
  final DateTime? calibratedAt;
  final DateTime createdAt;

  const Patient({
    required this.name,
    required this.age,
    required this.doctorName,
    this.baselineHr,
    this.baselineSpo2,
    this.calibratedAt,
    required this.createdAt,
  });

  Patient copyWith({String? name, int? age, String? doctorName, double? baselineHr, double? baselineSpo2, DateTime? calibratedAt}) =>
      Patient(
        name: name ?? this.name,
        age: age ?? this.age,
        doctorName: doctorName ?? this.doctorName,
        baselineHr: baselineHr ?? this.baselineHr,
        baselineSpo2: baselineSpo2 ?? this.baselineSpo2,
        calibratedAt: calibratedAt ?? this.calibratedAt,
        createdAt: createdAt,
      );

  Map<String, Object?> toRow() => {
        'id': 1,
        'name': name,
        'age': age,
        'doctor_name': doctorName,
        'baseline_hr': baselineHr,
        'baseline_spo2': baselineSpo2,
        'calibrated_at': calibratedAt?.millisecondsSinceEpoch,
        'created_at': createdAt.millisecondsSinceEpoch,
      };

  static Patient fromRow(Map<String, Object?> r) => Patient(
        name: r['name'] as String,
        age: r['age'] as int,
        doctorName: (r['doctor_name'] as String?) ?? '',
        baselineHr: (r['baseline_hr'] as num?)?.toDouble(),
        baselineSpo2: (r['baseline_spo2'] as num?)?.toDouble(),
        calibratedAt: _dt(r['calibrated_at']),
        createdAt: _dt(r['created_at']) ?? DateTime.now(),
      );
}

class Contact {
  final int? id;
  final String name;
  final String phone;
  const Contact({this.id, required this.name, required this.phone});
  Map<String, Object?> toRow() => {'id': id, 'name': name, 'phone': phone};
  static Contact fromRow(Map<String, Object?> r) =>
      Contact(id: r['id'] as int?, name: r['name'] as String, phone: r['phone'] as String);
}

class Episode {
  final int? id;
  final DateTime start;
  final DateTime? end;
  final String stateMax;
  final int doses;
  final int? minSpo2;
  final int? maxHr;
  final int coughs; // highest coughs-per-minute seen
  final bool emergencySent;
  final bool falseAlarm;

  const Episode({
    this.id,
    required this.start,
    this.end,
    this.stateMax = 'ALERT',
    this.doses = 0,
    this.minSpo2,
    this.maxHr,
    this.coughs = 0,
    this.emergencySent = false,
    this.falseAlarm = false,
  });

  bool get open => end == null;
  Duration duration(DateTime now) => (end ?? now).difference(start);

  Episode copyWith({
    int? id,
    DateTime? end,
    String? stateMax,
    int? doses,
    int? minSpo2,
    int? maxHr,
    int? coughs,
    bool? emergencySent,
    bool? falseAlarm,
  }) =>
      Episode(
        id: id ?? this.id,
        start: start,
        end: end ?? this.end,
        stateMax: stateMax ?? this.stateMax,
        doses: doses ?? this.doses,
        minSpo2: minSpo2 ?? this.minSpo2,
        maxHr: maxHr ?? this.maxHr,
        coughs: coughs ?? this.coughs,
        emergencySent: emergencySent ?? this.emergencySent,
        falseAlarm: falseAlarm ?? this.falseAlarm,
      );

  Map<String, Object?> toRow() => {
        'id': id,
        'start': start.millisecondsSinceEpoch,
        'end': end?.millisecondsSinceEpoch,
        'state_max': stateMax,
        'doses': doses,
        'min_spo2': minSpo2,
        'max_hr': maxHr,
        'coughs': coughs,
        'emergency_sent': emergencySent ? 1 : 0,
        'false_alarm': falseAlarm ? 1 : 0,
      };

  static Episode fromRow(Map<String, Object?> r) => Episode(
        id: r['id'] as int?,
        start: _dt(r['start'])!,
        end: _dt(r['end']),
        stateMax: (r['state_max'] as String?) ?? 'ALERT',
        doses: (r['doses'] as int?) ?? 0,
        minSpo2: r['min_spo2'] as int?,
        maxHr: r['max_hr'] as int?,
        coughs: (r['coughs'] as int?) ?? 0,
        emergencySent: r['emergency_sent'] == 1,
        falseAlarm: r['false_alarm'] == 1,
      );
}

class EventRow {
  final int? id;
  final int deviceEventId;
  final int? episodeId;
  final DateTime time;
  final String type;
  final String payloadJson;
  const EventRow({
    this.id,
    required this.deviceEventId,
    this.episodeId,
    required this.time,
    required this.type,
    required this.payloadJson,
  });
  Map<String, Object?> toRow() => {
        'id': id,
        'device_event_id': deviceEventId,
        'episode_id': episodeId,
        'time': time.millisecondsSinceEpoch,
        'type': type,
        'payload_json': payloadJson,
      };
  static EventRow fromRow(Map<String, Object?> r) => EventRow(
        id: r['id'] as int?,
        deviceEventId: r['device_event_id'] as int,
        episodeId: r['episode_id'] as int?,
        time: _dt(r['time'])!,
        type: r['type'] as String,
        payloadJson: (r['payload_json'] as String?) ?? '{}',
      );
}

class Reading {
  final DateTime time;
  final int hr;
  final int spo2;
  final bool valid;
  final int cough60;
  final String activity;
  final String state;
  const Reading({
    required this.time,
    required this.hr,
    required this.spo2,
    required this.valid,
    required this.cough60,
    required this.activity,
    required this.state,
  });
  Map<String, Object?> toRow() => {
        'time': time.millisecondsSinceEpoch,
        'hr': hr,
        'spo2': spo2,
        'valid': valid ? 1 : 0,
        'cough_60s': cough60,
        'activity': activity,
        'state': state,
      };
  static Reading fromRow(Map<String, Object?> r) => Reading(
        time: _dt(r['time'])!,
        hr: r['hr'] as int,
        spo2: r['spo2'] as int,
        valid: r['valid'] == 1,
        cough60: (r['cough_60s'] as int?) ?? 0,
        activity: (r['activity'] as String?) ?? 'REST',
        state: (r['state'] as String?) ?? 'NORMAL',
      );
}

class Dose {
  final int? id;
  final int episodeId;
  final DateTime time;
  const Dose({this.id, required this.episodeId, required this.time});
  Map<String, Object?> toRow() => {'id': id, 'episode_id': episodeId, 'time': time.millisecondsSinceEpoch};
  static Dose fromRow(Map<String, Object?> r) =>
      Dose(id: r['id'] as int?, episodeId: r['episode_id'] as int, time: _dt(r['time'])!);
}

DateTime? _dt(Object? ms) => ms == null ? null : DateTime.fromMillisecondsSinceEpoch(ms as int);
