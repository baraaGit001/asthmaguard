// Signals -> rules (section 4.2). Pure logic, ticked once per second.
#pragma once
#include <stdint.h>

#include "config.h"
#include "types.h"

namespace ag {

struct Rules {
  bool cough = false;          // COUGH_SIGN
  bool hr = false;             // HR_SIGN (valid reading only)
  bool spo2 = false;           // SPO2_SIGN (valid reading only)
  bool spo2_critical = false;  // SPO2_CRITICAL (valid < crit for N s in a row)
  bool not_exercise = true;    // NOT_EXERCISE
  bool attack = false;         // ATTACK_CONDITION
  bool any_sign = false;       // cough || hr || spo2
  bool improved = false;       // instant "improved" check (MONITOR holds it 3 min)
};

class Detector {
 public:
  explicit Detector(const Config& cfg) : cfg_(cfg) {}
  Rules update(const Signals& s);  // call once per second
  void reset() { spo2_low_s_ = 0; }
  uint16_t spo2LowSeconds() const { return spo2_low_s_; }

 private:
  const Config& cfg_;
  uint16_t spo2_low_s_ = 0;
};

// Counts cough events in a sliding 60 s window, ignoring events closer than
// cfg.cough_gap_ms to the previous one and scores below cfg.cough_score_min.
class CoughCounter {
 public:
  explicit CoughCounter(const Config& cfg) : cfg_(cfg) {}
  // Returns true if the event was counted.
  bool onScore(float score, uint32_t now_ms);
  uint8_t count60(uint32_t now_ms);
  void clear() { n_ = 0; head_ = 0; has_last_ = false; }

 private:
  static const uint8_t kMax = 64;
  const Config& cfg_;
  uint32_t t_[kMax] = {};
  uint8_t n_ = 0, head_ = 0;
  uint32_t last_ms_ = 0;
  bool has_last_ = false;
  bool above_ = false;  // score currently above threshold (one event per burst)
};

}  // namespace ag
