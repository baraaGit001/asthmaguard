#include "imu.h"

namespace ag {

namespace {
const uint8_t REG_SMPLRT_DIV = 0x19, REG_CONFIG = 0x1A, REG_GYRO_CFG = 0x1B, REG_ACCEL_CFG = 0x1C,
              REG_ACCEL_X = 0x3B, REG_PWR1 = 0x6B, REG_WHOAMI = 0x75;
const float kAccelLsbPerG = 8192.0f;  // +-4 g
const uint32_t kPeriodMs = 20;        // 50 Hz
}  // namespace

static bool wr(TwoWire* w, uint8_t addr, uint8_t reg, uint8_t v) {
  w->beginTransmission(addr);
  w->write(reg);
  w->write(v);
  return w->endTransmission() == 0;
}

bool Imu::begin(TwoWire& w, uint8_t addr) {
  w_ = &w;
  addr_ = addr;
  w_->beginTransmission(addr_);
  w_->write(REG_WHOAMI);
  if (w_->endTransmission(false) != 0 || w_->requestFrom(addr_, (uint8_t)1) != 1) return ok_ = false;
  uint8_t who = w_->read();
  // 0x68 = MPU6050; 0x70/0x72/0x98 are common clones that work the same way.
  (void)who;
  ok_ = wr(w_, addr_, REG_PWR1, 0x01)            // wake, PLL with X gyro
        && wr(w_, addr_, REG_CONFIG, 0x03)       // DLPF ~44 Hz
        && wr(w_, addr_, REG_SMPLRT_DIV, 19)     // 1 kHz / 20 = 50 Hz
        && wr(w_, addr_, REG_ACCEL_CFG, 0x08)    // +-4 g
        && wr(w_, addr_, REG_GYRO_CFG, 0x08);    // +-500 dps (unused for now)
  return ok_;
}

bool Imu::readAccel() {
  w_->beginTransmission(addr_);
  w_->write(REG_ACCEL_X);
  if (w_->endTransmission(false) != 0 || w_->requestFrom(addr_, (uint8_t)6) != 6) {
    errors_++;
    return false;
  }
  int16_t v[3];
  for (int i = 0; i < 3; i++) {
    uint8_t hi = w_->read(), lo = w_->read();
    v[i] = (int16_t)((hi << 8) | lo);
  }
  ax_ = v[0] / kAccelLsbPerG;
  ay_ = v[1] / kAccelLsbPerG;
  az_ = v[2] / kAccelLsbPerG;
  return true;
}

bool Imu::poll(uint32_t now_ms) {
  if (!ok_ || (int32_t)(now_ms - next_ms_) < 0) return false;
  next_ms_ = now_ms + kPeriodMs;
  if (!readAccel()) return false;
  act_.addSample(ax_, ay_, az_);
  float p = pitchFromAccel(ax_, ay_, az_);
  // light low-pass (~100 ms) so a wrist flick does not look like a raise
  pitch_ = have_pitch_ ? pitch_ + 0.2f * (p - pitch_) : p;
  have_pitch_ = true;
  return raise_.update(pitch_, now_ms);
}

}  // namespace ag
