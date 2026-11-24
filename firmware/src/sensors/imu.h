// MPU6050 at 50 Hz: activity level, "still" flag and arm-raise events.
// Register-level driver (no library). I2C address 0x68 on Wire (GP4/GP5).
#pragma once
#include <Wire.h>
#include <stdint.h>

#include "../config.h"
#include "motion.h"

namespace ag {

class Imu {
 public:
  explicit Imu(const Config& cfg) : raise_(cfg) {}
  bool begin(TwoWire& w, uint8_t addr = 0x68);
  // Call every loop; samples at 50 Hz. Returns true once per detected raise.
  bool poll(uint32_t now_ms);
  void endSecond() { act_.endSecond(); }  // once per second

  bool ok() const { return ok_; }
  Activity activity() const { return act_.activity(); }
  bool stillFor5s() const { return ok_ && act_.stillFor5s(); }
  float pitch() const { return pitch_; }
  float ax() const { return ax_; }
  float ay() const { return ay_; }
  float az() const { return az_; }
  float motionStd() const { return act_.lastStd(); }
  uint32_t errors() const { return errors_; }

 private:
  bool readAccel();
  TwoWire* w_ = nullptr;
  uint8_t addr_ = 0x68;
  bool ok_ = false;
  uint32_t next_ms_ = 0, errors_ = 0;
  float ax_ = 0, ay_ = 0, az_ = 1, pitch_ = 0;
  bool have_pitch_ = false;
  ActivityTracker act_;
  ArmRaiseDetector raise_;
};

}  // namespace ag
