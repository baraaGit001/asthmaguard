// Device config fields editable in the doctor area. Ranges mirror
// firmware/src/config.cpp (the device rejects anything outside them).

class ConfigField {
  final String key;
  final String ar;
  final String en;
  final num min;
  final num max;
  final bool isInt;
  final String unit;
  const ConfigField(this.key, this.ar, this.en, this.min, this.max,
      {this.isInt = true, this.unit = ''});

  bool valid(num v) => v >= min && v <= max && (!isInt || v == v.roundToDouble());
}

class ConfigGroup {
  final String ar;
  final String en;
  final List<ConfigField> fields;
  const ConfigGroup(this.ar, this.en, this.fields);
}

const doseFields = [
  ConfigField('max_doses', 'أقصى عدد بخات في النوبة', 'Max puffs per episode', 0, 8),
  ConfigField('dose_interval_s', 'أقل وقت بين بختين', 'Min time between puffs', 0, 86400, unit: 's'),
];

const configGroups = [
  ConfigGroup('الكحة', 'Cough', [
    ConfigField('cough_sign', 'عدد الكحات في الدقيقة (علامة)', 'Coughs per minute (sign)', 1, 30),
    ConfigField('cough_score_min', 'حد ثقة النموذج', 'Model score threshold', 0.5, 0.99, isInt: false),
  ]),
  ConfigGroup('النبض والأكسجين', 'Heart rate & SpO2', [
    ConfigField('hr_rise', 'زيادة النبض عن المعتاد', 'HR rise over baseline', 5, 60, unit: 'bpm'),
    ConfigField('hr_abs', 'نبض مرتفع مطلق', 'High HR (absolute)', 80, 200, unit: 'bpm'),
    ConfigField('spo2_sign', 'أكسجين منخفض (علامة)', 'Low SpO2 (sign)', 85, 99, unit: '%'),
    ConfigField('spo2_crit', 'أكسجين حرج أقل من', 'Critical SpO2 below', 80, 98, unit: '%'),
    ConfigField('spo2_crit_s', 'مدة الأكسجين الحرج', 'Critical SpO2 duration', 5, 120, unit: 's'),
  ]),
  ConfigGroup('التوقيتات', 'Timing', [
    ConfigField('attack_hold_s', 'مدة استمرار العلامات قبل التنبيه', 'Signs held before alert', 5, 300, unit: 's'),
    ConfigField('cancel_window_s', 'مهلة إلغاء الإنذار الكاذب', 'False-alarm cancel window', 5, 60, unit: 's'),
    ConfigField('raise_timeout_s', 'مهلة رفع الذراع قبل الطوارئ', 'Arm-raise timeout', 15, 600, unit: 's'),
    ConfigField('improve_hold_s', 'مدة التحسن المطلوبة', 'Improvement hold', 30, 1800, unit: 's'),
    ConfigField('monitor_timeout_s', 'مدة المراقبة بعد البخة', 'Monitoring after a puff', 60, 3600, unit: 's'),
  ]),
  ConfigGroup('التحسن', 'Improvement', [
    ConfigField('improve_spo2', 'أكسجين التحسن على الأقل', 'Improved SpO2 at least', 85, 99, unit: '%'),
    ConfigField('improve_hr_margin', 'نبض التحسن (فوق المعتاد بحد أقصى)', 'Improved HR margin', 0, 60, unit: 'bpm'),
  ]),
  ConfigGroup('المعدل الأساسي', 'Baseline', [
    ConfigField('baseline_hr', 'النبض الأساسي', 'Baseline HR', 40, 120, isInt: false, unit: 'bpm'),
    ConfigField('baseline_spo2', 'الأكسجين الأساسي', 'Baseline SpO2', 85, 100, isInt: false, unit: '%'),
  ]),
  ConfigGroup('البخاخ', 'Inhaler servo', [
    ConfigField('servo_rest', 'زاوية الراحة', 'Rest angle', 0, 180, unit: '°'),
    ConfigField('servo_press', 'زاوية الضغط', 'Press angle', 0, 180, unit: '°'),
    ConfigField('servo_hold_ms', 'مدة الضغط', 'Press hold', 100, 3000, unit: 'ms'),
  ]),
];

/// Validates doctor dose limits: both must be set (> 0) for dosing to be
/// enabled; zero for both disables dosing again.
String? validateDoseLimits(num? maxDoses, num? intervalS) {
  if (maxDoses == null || intervalS == null) return 'missing';
  if (maxDoses < 0 || maxDoses > 8) return 'max_doses';
  if (intervalS < 0 || intervalS > 86400) return 'interval';
  final setOne = (maxDoses > 0) != (intervalS > 0);
  if (setOne) return 'both';
  return null;
}
