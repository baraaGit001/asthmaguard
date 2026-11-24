import 'package:path/path.dart' as p;
import 'package:sqflite/sqflite.dart';

import 'models.dart';
import 'store.dart';

class SqfliteStore implements Store {
  final Database db;
  SqfliteStore._(this.db);

  static Future<SqfliteStore> open() async {
    final path = p.join(await getDatabasesPath(), 'asthmaguard.db');
    final db = await openDatabase(path, version: 1, onCreate: (db, _) async {
      await db.execute('''CREATE TABLE patient(id INTEGER PRIMARY KEY, name TEXT NOT NULL, age INTEGER NOT NULL,
        doctor_name TEXT, baseline_hr REAL, baseline_spo2 REAL, calibrated_at INTEGER, created_at INTEGER)''');
      await db.execute('CREATE TABLE contact(id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, phone TEXT NOT NULL)');
      await db.execute('''CREATE TABLE episode(id INTEGER PRIMARY KEY AUTOINCREMENT, start INTEGER NOT NULL, "end" INTEGER,
        state_max TEXT, doses INTEGER DEFAULT 0, min_spo2 INTEGER, max_hr INTEGER, coughs INTEGER DEFAULT 0,
        emergency_sent INTEGER DEFAULT 0, false_alarm INTEGER DEFAULT 0)''');
      await db.execute('''CREATE TABLE event(id INTEGER PRIMARY KEY AUTOINCREMENT, device_event_id INTEGER NOT NULL UNIQUE,
        episode_id INTEGER, time INTEGER NOT NULL, type TEXT NOT NULL, payload_json TEXT)''');
      await db.execute('''CREATE TABLE reading(time INTEGER NOT NULL, hr INTEGER, spo2 INTEGER, valid INTEGER,
        cough_60s INTEGER, activity TEXT, state TEXT)''');
      await db.execute('CREATE INDEX reading_time ON reading(time)');
      await db.execute('CREATE TABLE dose(id INTEGER PRIMARY KEY AUTOINCREMENT, episode_id INTEGER NOT NULL, time INTEGER NOT NULL)');
    });
    return SqfliteStore._(db);
  }

  @override
  Future<Patient?> patient() async {
    final r = await db.query('patient', where: 'id = 1');
    return r.isEmpty ? null : Patient.fromRow(r.first);
  }

  @override
  Future<void> savePatient(Patient pt) async =>
      db.insert('patient', pt.toRow(), conflictAlgorithm: ConflictAlgorithm.replace);

  @override
  Future<List<Contact>> contacts() async => (await db.query('contact', orderBy: 'id')).map(Contact.fromRow).toList();

  @override
  Future<void> replaceContacts(List<Contact> c) async {
    await db.transaction((t) async {
      await t.delete('contact');
      for (final x in c) {
        await t.insert('contact', {'name': x.name, 'phone': x.phone});
      }
    });
  }

  @override
  Future<Episode?> openEpisode() async {
    final r = await db.query('episode', where: '"end" IS NULL', orderBy: 'start DESC', limit: 1);
    return r.isEmpty ? null : Episode.fromRow(r.first);
  }

  @override
  Future<int> insertEpisode(Episode e) async => db.insert('episode', e.toRow()..remove('id'));

  @override
  Future<void> updateEpisode(Episode e) async => db.update('episode', e.toRow(), where: 'id = ?', whereArgs: [e.id]);

  @override
  Future<Episode?> episode(int id) async {
    final r = await db.query('episode', where: 'id = ?', whereArgs: [id]);
    return r.isEmpty ? null : Episode.fromRow(r.first);
  }

  @override
  Future<List<Episode>> episodes({DateTime? from, DateTime? to}) async {
    final where = <String>[];
    final args = <Object>[];
    if (from != null) {
      where.add('start >= ?');
      args.add(from.millisecondsSinceEpoch);
    }
    if (to != null) {
      where.add('start < ?');
      args.add(to.millisecondsSinceEpoch);
    }
    final r = await db.query('episode',
        where: where.isEmpty ? null : where.join(' AND '), whereArgs: args, orderBy: 'start DESC');
    return r.map(Episode.fromRow).toList();
  }

  @override
  Future<bool> insertEvent(EventRow e) async {
    final id = await db.insert('event', e.toRow()..remove('id'), conflictAlgorithm: ConflictAlgorithm.ignore);
    return id != 0;
  }

  @override
  Future<List<EventRow>> events({int? episodeId, DateTime? from, DateTime? to}) async {
    final where = <String>[];
    final args = <Object>[];
    if (episodeId != null) {
      where.add('episode_id = ?');
      args.add(episodeId);
    }
    if (from != null) {
      where.add('time >= ?');
      args.add(from.millisecondsSinceEpoch);
    }
    if (to != null) {
      where.add('time < ?');
      args.add(to.millisecondsSinceEpoch);
    }
    final r = await db.query('event',
        where: where.isEmpty ? null : where.join(' AND '), whereArgs: args, orderBy: 'time');
    return r.map(EventRow.fromRow).toList();
  }

  @override
  Future<void> insertReading(Reading r) async => db.insert('reading', r.toRow());

  @override
  Future<List<Reading>> readings(DateTime from, DateTime to) async {
    final r = await db.query('reading',
        where: 'time >= ? AND time < ?',
        whereArgs: [from.millisecondsSinceEpoch, to.millisecondsSinceEpoch],
        orderBy: 'time');
    return r.map(Reading.fromRow).toList();
  }

  @override
  Future<void> insertDose(Dose d) async => db.insert('dose', d.toRow()..remove('id'));

  @override
  Future<List<Dose>> doses(int episodeId) async =>
      (await db.query('dose', where: 'episode_id = ?', whereArgs: [episodeId], orderBy: 'time')).map(Dose.fromRow).toList();
}
