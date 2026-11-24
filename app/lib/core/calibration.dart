// Baseline calibration (section 6.4): for the first 3 days, valid readings
// taken while the IMU says REST; baseline = median HR and median SpO2.
import '../data/models.dart';

class Baseline {
  final double hr;
  final double spo2;
  final int samples;
  const Baseline(this.hr, this.spo2, this.samples);
}

const calibrationPeriod = Duration(days: 3);
const minCalibrationSamples = 30;

double _median(List<num> v) {
  final s = List<num>.of(v)..sort();
  final n = s.length;
  return n.isOdd ? s[n ~/ 2].toDouble() : (s[n ~/ 2 - 1] + s[n ~/ 2]) / 2.0;
}

/// Returns null when there are too few usable readings.
Baseline? computeBaseline(Iterable<Reading> readings) {
  final use = readings
      .where((r) => r.valid && r.activity == 'REST' && r.state == 'NORMAL' && r.hr > 30 && r.spo2 >= 80)
      .toList();
  if (use.length < minCalibrationSamples) return null;
  return Baseline(_median([for (final r in use) r.hr]), _median([for (final r in use) r.spo2]), use.length);
}

/// Calibration is due once the 3 days are over and it has not been done.
bool calibrationDue(Patient p, DateTime now) =>
    p.calibratedAt == null && now.difference(p.createdAt) >= calibrationPeriod;
