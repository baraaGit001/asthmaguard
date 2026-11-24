#include "motion.h"

#include <math.h>

namespace ag {

void ActivityTracker::addSample(float ax, float ay, float az) {
  double m = sqrt((double)ax * ax + (double)ay * ay + (double)az * az);
  sum_ += m;
  sum2_ += m * m;
  n_++;
}

void ActivityTracker::endSecond() {
  float sd = 0;
  if (n_ > 1) {
    double mean = sum_ / n_;
    double var = sum2_ / n_ - mean * mean;
    sd = var > 0 ? (float)sqrt(var) : 0;
  } else {
    sd = 1.0f;  // no samples = IMU problem; never call it "still"
  }
  last_std_ = sd;
  hist_[head_] = sd;
  head_ = (head_ + 1) % 60;
  if (count_ < 60) count_++;
  still_run_ = sd < t_.still_std ? (still_run_ < 255 ? still_run_ + 1 : 255) : 0;
  sum_ = sum2_ = 0;
  n_ = 0;
}

float ActivityTracker::meanStd60() const {
  if (!count_) return 0;
  float s = 0;
  for (uint8_t i = 0; i < count_; i++) s += hist_[i];
  return s / count_;
}

Activity ActivityTracker::activity() const {
  float m = meanStd60();
  if (m >= t_.exercise_std) return Activity::EXERCISE;
  if (m >= t_.rest_std) return Activity::LIGHT;
  return Activity::REST;
}

float pitchFromAccel(float ax, float ay, float az) {
  return (float)(atan2((double)ax, sqrt((double)ay * ay + (double)az * az)) * 180.0 / M_PI);
}

bool ArmRaiseDetector::update(float pitch, uint32_t now_ms) {
  p_[head_] = pitch;
  t_[head_] = now_ms;
  head_ = (head_ + 1) % kHist;
  if (n_ < kHist) n_++;

  const float need = cfg_.raise_deg;
  const int8_t dir = cfg_.raise_invert ? -1 : 1;  // only the raising direction counts
  if (!armed_) {
    // Re-arm once the arm is back down.
    if (dir_ * (pitch - ref_) < need * 0.5f) armed_ = true;
    return false;
  }
  if (candidate_) {
    if (dir_ * (pitch - ref_) < need * 0.75f) {
      candidate_ = false;  // dropped before the hold time
    } else if (now_ms - since_ >= cfg_.raise_hold_ms) {
      candidate_ = false;
      armed_ = false;
      return true;
    }
    return false;
  }
  // Largest change against any sample in the last raise_within_ms.
  float best = 0, ref = pitch;
  for (uint8_t i = 0; i < n_; i++) {
    uint8_t idx = (head_ + kHist - 1 - i) % kHist;
    if (now_ms - t_[idx] > cfg_.raise_within_ms) break;
    float d = dir * (pitch - p_[idx]);
    if (d > best) { best = d; ref = p_[idx]; }
  }
  if (best >= need) {
    candidate_ = true;
    ref_ = ref;
    dir_ = dir;
    since_ = now_ms;
  }
  return false;
}

}  // namespace ag
