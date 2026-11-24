// MPU6050 at 50 Hz: activity level, "still" flag and arm-raise events
// (port of the Pico imu.cpp; same registers, same detectors).
#pragma once
#include <stdint.h>

#include "config.h"
#include "hw/i2c.h"
#include "sensors/motion.h"

namespace ag {

class Mpu6050 {
 public:
  explicit Mpu6050(const Config& cfg) : raise_(cfg) {}
  bool begin(I2cBus& bus, uint8_t addr = 0x68);
  // Call every loop; samples at 50 Hz. Returns true once per detected raise.
  bool poll(uint32_t now_ms);
  void endSecond() { act_.endSecond(); }

  bool ok() const { return ok_; }
  uint8_t whoAmI() const { return who_; }
  Activity activity() const { return act_.activity(); }
  bool stillFor5s() const { return ok_ && act_.stillFor5s(); }
  float pitch() const { return pitch_; }
  float ax() const { return ax_; }
  float ay() const { return ay_; }
  float az() const { return az_; }
  float motionStd() const { return act_.lastStd(); }

 private:
  I2cBus* bus_ = nullptr;
  uint8_t addr_ = 0x68, who_ = 0;
  bool ok_ = false, have_pitch_ = false;
  uint32_t next_ms_ = 0;
  float ax_ = 0, ay_ = 0, az_ = 1, pitch_ = 0;
  ActivityTracker act_;
  ArmRaiseDetector raise_;
};

}  // namespace ag
