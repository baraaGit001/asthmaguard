// Storage interface + an in-memory implementation (tests). The app uses
// SqfliteStore (sqlite_store.dart).
import 'models.dart';

abstract class Store {
  Future<Patient?> patient();
  Future<void> savePatient(Patient p);

  Future<List<Contact>> contacts();
  Future<void> replaceContacts(List<Contact> c);

  Future<Episode?> openEpisode();
  Future<int> insertEpisode(Episode e);
  Future<void> updateEpisode(Episode e);
  Future<Episode?> episode(int id);
  Future<List<Episode>> episodes({DateTime? from, DateTime? to});

  /// Returns false if an event with this device id was already stored.
  Future<bool> insertEvent(EventRow e);
  Future<List<EventRow>> events({int? episodeId, DateTime? from, DateTime? to});

  Future<void> insertReading(Reading r);
  Future<List<Reading>> readings(DateTime from, DateTime to);

  Future<void> insertDose(Dose d);
  Future<List<Dose>> doses(int episodeId);
}

class MemoryStore implements Store {
  Patient? _patient;
  final List<Contact> _contacts = [];
  final List<Episode> _episodes = [];
  final List<EventRow> _events = [];
  final List<Reading> _readings = [];
  final List<Dose> _doses = [];

  @override
  Future<Patient?> patient() async => _patient;
  @override
  Future<void> savePatient(Patient p) async => _patient = p;

  @override
  Future<List<Contact>> contacts() async => List.of(_contacts);
  @override
  Future<void> replaceContacts(List<Contact> c) async {
    _contacts
      ..clear()
      ..addAll([for (var i = 0; i < c.length; i++) Contact(id: i + 1, name: c[i].name, phone: c[i].phone)]);
  }

  @override
  Future<Episode?> openEpisode() async {
    for (final e in _episodes.reversed) {
      if (e.open) return e;
    }
    return null;
  }

  @override
  Future<int> insertEpisode(Episode e) async {
    final id = _episodes.length + 1;
    _episodes.add(e.copyWith(id: id));
    return id;
  }

  @override
  Future<void> updateEpisode(Episode e) async {
    final i = _episodes.indexWhere((x) => x.id == e.id);
    if (i >= 0) _episodes[i] = e;
  }

  @override
  Future<Episode?> episode(int id) async {
    for (final e in _episodes) {
      if (e.id == id) return e;
    }
    return null;
  }

  @override
  Future<List<Episode>> episodes({DateTime? from, DateTime? to}) async => _episodes
      .where((e) => (from == null || !e.start.isBefore(from)) && (to == null || e.start.isBefore(to)))
      .toList()
      .reversed
      .toList();

  @override
  Future<bool> insertEvent(EventRow e) async {
    if (_events.any((x) => x.deviceEventId == e.deviceEventId)) return false;
    _events.add(e);
    return true;
  }

  @override
  Future<List<EventRow>> events({int? episodeId, DateTime? from, DateTime? to}) async => _events
      .where((e) =>
          (episodeId == null || e.episodeId == episodeId) &&
          (from == null || !e.time.isBefore(from)) &&
          (to == null || e.time.isBefore(to)))
      .toList();

  @override
  Future<void> insertReading(Reading r) async => _readings.add(r);
  @override
  Future<List<Reading>> readings(DateTime from, DateTime to) async =>
      _readings.where((r) => !r.time.isBefore(from) && r.time.isBefore(to)).toList();

  @override
  Future<void> insertDose(Dose d) async => _doses.add(d);
  @override
  Future<List<Dose>> doses(int episodeId) async => _doses.where((d) => d.episodeId == episodeId).toList();
}
