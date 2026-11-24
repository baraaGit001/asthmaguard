#include "detector.h"

namespace ag {

Rules Detector::update(const Signals& s) {
  Rules r;
  const Config& c = cfg_;
  r.cough = s.cough_60s >= c.cough_sign;
  if (s.ppg_valid) {
    r.hr = s.hr >= c.baseline_hr + c.hr_rise || s.hr >= c.hr_abs;
    r.spo2 = s.spo2 <= c.spo2_sign;
  }
  // SPO2_CRITICAL needs a valid low reading every second of the run. An
  // invalid second (wrist moving) breaks the run, as the spec says
  // "invalid readings are never used for decisions".
  if (s.ppg_valid && s.spo2 < c.spo2_crit) {
    if (spo2_low_s_ < 0xFFFF) spo2_low_s_++;
  } else {
    spo2_low_s_ = 0;
  }
  r.spo2_critical = spo2_low_s_ >= c.spo2_crit_s;
  r.not_exercise = s.activity != Activity::EXERCISE;
  r.attack = r.cough && (r.hr || r.spo2) && r.not_exercise;
  r.any_sign = r.cough || r.hr || r.spo2;
  r.improved = s.ppg_valid && s.spo2 >= c.improve_spo2 &&
               s.hr <= c.baseline_hr + c.improve_hr_margin &&
               s.cough_60s <= c.improve_cough_max;
  return r;
}

// One cough shows up in several overlapping 1 s windows (inference every
// 250 ms), so an event is counted on the rising edge of the score. While the
// score stays high (a coughing fit) one more event is counted every second.
bool CoughCounter::onScore(float score, uint32_t now_ms) {
  bool high = score >= cfg_.cough_score_min;
  bool count = false;
  if (high) {
    uint32_t gap = above_ ? 1000u : cfg_.cough_gap_ms;
    if (!has_last_ || now_ms - last_ms_ >= gap) count = true;
  }
  above_ = high;
  if (!count) return false;
  last_ms_ = now_ms;
  has_last_ = true;
  t_[head_] = now_ms;
  head_ = (head_ + 1) % kMax;
  if (n_ < kMax) n_++;
  return true;
}

uint8_t CoughCounter::count60(uint32_t now_ms) {
  uint8_t c = 0;
  for (uint8_t i = 0; i < n_; i++) {
    uint8_t idx = (head_ + kMax - 1 - i) % kMax;
    if (now_ms - t_[idx] < 60000u) c++;
    else break;  // older entries are older still
  }
  return c;
}

}  // namespace ag
