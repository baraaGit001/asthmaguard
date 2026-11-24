// Turns the device's events and live readings into stored episodes,
// events, readings and doses (section 6.3). Pure Dart over [Store].
import '../data/models.dart';
import '../data/store.dart';
import 'protocol.dart';

class EpisodeTracker {
  final Store store;
  final Duration normalEvery;
  final Duration episodeEvery;
  DateTime? _lastReading;

  EpisodeTracker(this.store,
      {this.normalEvery = const Duration(seconds: 60), this.episodeEvery = const Duration(seconds: 5)});

  /// Stores one device event. Returns false if it was a duplicate (the
  /// device re-sends until it gets an ACK; the caller ACKs either way).
  Future<bool> onEvent(DeviceEvent e) async {
    var ep = await store.openEpisode();
    final t = e.time;

    // Episode start
    if (ep == null && (e.type == 'ATTACK_START' || e.isEmergency)) {
      final id = await store.insertEpisode(Episode(
        start: t,
        stateMax: e.isEmergency ? 'EMERGENCY' : 'ALERT',
        minSpo2: e.validReading ? e.spo2 : null,
        maxHr: e.validReading ? e.hr : null,
        coughs: (e.data['c60'] as num?)?.toInt() ?? 0,
      ));
      ep = await store.episode(id);
    }

    final fresh = await store.insertEvent(EventRow(
      deviceEventId: e.id,
      episodeId: ep?.id,
      time: t,
      type: e.type,
      payloadJson: e.toJson(),
    ));
    if (!fresh || ep == null) return fresh;

    switch (e.type) {
      case 'DOSE':
        await store.insertDose(Dose(episodeId: ep.id!, time: t));
        ep = ep.copyWith(doses: ep.doses + 1);
        break;
      case 'EMERGENCY':
      case 'MANUAL_SOS':
        ep = ep.copyWith(stateMax: 'EMERGENCY');
        break;
      case 'FALSE_ALARM':
        ep = ep.copyWith(end: t, falseAlarm: true);
        break;
      case 'RESOLVED':
        ep = ep.copyWith(end: t);
        break;
      case 'BOOT':
        // Device restarted in the middle of an episode: close it so it does
        // not stay open forever (the device starts again in NORMAL).
        ep = ep.copyWith(end: t);
        break;
    }
    ep = _withVitals(ep, e.validReading ? e.hr : null, e.validReading ? e.spo2 : null,
        (e.data['c60'] as num?)?.toInt());
    await store.updateEpisode(ep);
    return true;
  }

  Future<void> markEmergencySent() async {
    final ep = await store.openEpisode();
    if (ep != null) await store.updateEpisode(ep.copyWith(emergencySent: true));
  }

  /// Live reading at 1 Hz. Stored every 60 s normally, every 5 s during an
  /// episode; the open episode's min/max are updated on every valid reading.
  Future<void> onLive(LiveData l) async {
    var ep = await store.openEpisode();
    if (ep == null && isEpisodeState(l.state)) {
      // An episode is going on but its start event has not arrived yet
      // (e.g. it is still queued on the device). Open it now.
      final id = await store.insertEpisode(Episode(start: l.at, stateMax: l.state));
      ep = await store.episode(id);
    }
    if (ep != null) {
      var next = _withVitals(ep, l.valid ? l.hr : null, l.valid ? l.spo2 : null, l.cough60);
      if (stateRank(l.state) > stateRank(next.stateMax)) next = next.copyWith(stateMax: l.state);
      if (!identical(next, ep)) await store.updateEpisode(next);
    }
    final every = ep != null ? episodeEvery : normalEvery;
    if (_lastReading == null || l.at.difference(_lastReading!) >= every) {
      _lastReading = l.at;
      await store.insertReading(Reading(
        time: l.at,
        hr: l.hr,
        spo2: l.spo2,
        valid: l.valid,
        cough60: l.cough60,
        activity: l.activity,
        state: l.state,
      ));
    }
  }

  Episode _withVitals(Episode ep, int? hr, int? spo2, int? c60) {
    var out = ep;
    if (spo2 != null && spo2 > 0 && (out.minSpo2 == null || spo2 < out.minSpo2!)) out = out.copyWith(minSpo2: spo2);
    if (hr != null && hr > 0 && (out.maxHr == null || hr > out.maxHr!)) out = out.copyWith(maxHr: hr);
    if (c60 != null && c60 > out.coughs) out = out.copyWith(coughs: c60);
    return out;
  }
}
