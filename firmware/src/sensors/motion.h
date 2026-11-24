// IMU-derived logic (pure, testable on the host):
//  - Activity level over the last 60 s (REST / LIGHT / EXERCISE)
//  - "Wrist still for the last 5 s" flag used to validate PPG readings
//  - Arm-raise detector: pitch changes >= raise_deg within raise_within_ms
//    and stays there >= raise_hold_ms.
#pragma once
#include <stdint.h>

#include "../config.h"
#include "../types.h"

namespace ag {

// Thresholds on the per-second standard deviation of |accel| (in g).
// Starting values; tune with real recordings (phase 7).
struct MotionTuning {
  float still_std = 0.02f;      // a second counts as "still" below this
  float rest_std = 0.04f;       // 60 s mean below this -> REST
  float exercise_std = 0.25f;   // 60 s mean at/above this -> EXERCISE
  uint8_t still_seconds = 5;
};

class ActivityTracker {
 public:
  explicit ActivityTracker(const MotionTuning& t = MotionTuning()) : t_(t) {}
  // One accelerometer sample (g). Call at the IMU rate.
  void addSample(float ax, float ay, float az);
  // Close the current 1 s bucket. Call once per second.
  void endSecond();
  Activity activity() const;
  bool stillFor5s() const { return still_run_ >= t_.still_seconds; }
  float lastStd() const { return last_std_; }
  float meanStd60() const;

 private:
  MotionTuning t_;
  double sum_ = 0, sum2_ = 0;
  uint16_t n_ = 0;
  float hist_[60] = {};
  uint8_t head_ = 0, count_ = 0;
  uint8_t still_run_ = 0;
  float last_std_ = 0;
};

class ArmRaiseDetector {
 public:
  explicit ArmRaiseDetector(const Config& cfg) : cfg_(cfg) {}
  // pitch in degrees at ~50 Hz. Returns true once per raise.
  bool update(float pitch_deg, uint32_t now_ms);

 private:
  static const uint8_t kHist = 100;  // 2 s at 50 Hz
  const Config& cfg_;
  float p_[kHist] = {};
  uint32_t t_[kHist] = {};
  uint8_t head_ = 0, n_ = 0;
  bool candidate_ = false, armed_ = true;
  float ref_ = 0;
  int8_t dir_ = 1;
  uint32_t since_ = 0;
};

// Pitch of the forearm axis (sensor X) against the horizon, from gravity.
float pitchFromAccel(float ax, float ay, float az);

}  // namespace ag
