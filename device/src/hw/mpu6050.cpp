#include "hw/mpu6050.h"

namespace ag {

namespace {
const uint8_t REG_SMPLRT_DIV = 0x19, REG_CONFIG = 0x1A, REG_GYRO_CFG = 0x1B, REG_ACCEL_CFG = 0x1C,
              REG_ACCEL_X = 0x3B, REG_PWR1 = 0x6B, REG_WHOAMI = 0x75;
const float kAccelLsbPerG = 8192.0f;  // +-4 g
const uint32_t kPeriodMs = 20;        // 50 Hz
}  // namespace

bool Mpu6050::begin(I2cBus& bus, uint8_t addr) {
  bus_ = &bus;
  addr_ = addr;
  // 0x68 = MPU6050; 0x70 / 0x72 / 0x98 are common clones that work the same way.
  if (!bus_->readReg(addr_, REG_WHOAMI, who_)) return ok_ = false;
  ok_ = bus_->writeReg(addr_, REG_PWR1, 0x01)          // wake, PLL with X gyro
        && bus_->writeReg(addr_, REG_CONFIG, 0x03)     // DLPF ~44 Hz
        && bus_->writeReg(addr_, REG_SMPLRT_DIV, 19)   // 1 kHz / 20 = 50 Hz
        && bus_->writeReg(addr_, REG_ACCEL_CFG, 0x08)  // +-4 g
        && bus_->writeReg(addr_, REG_GYRO_CFG, 0x08);  // +-500 dps (unused for now)
  return ok_;
}

bool Mpu6050::poll(uint32_t now_ms) {
  if (!ok_ || (int32_t)(now_ms - next_ms_) < 0) return false;
  next_ms_ = now_ms + kPeriodMs;
  uint8_t b[6];
  if (!bus_->readRegs(addr_, REG_ACCEL_X, b, 6)) return false;
  ax_ = (int16_t)((b[0] << 8) | b[1]) / kAccelLsbPerG;
  ay_ = (int16_t)((b[2] << 8) | b[3]) / kAccelLsbPerG;
  az_ = (int16_t)((b[4] << 8) | b[5]) / kAccelLsbPerG;
  act_.addSample(ax_, ay_, az_);
  float p = pitchFromAccel(ax_, ay_, az_);
  // light low-pass (~100 ms) so a wrist flick does not look like a raise
  pitch_ = have_pitch_ ? pitch_ + 0.2f * (p - pitch_) : p;
  have_pitch_ = true;
  return raise_.update(pitch_, now_ms);
}

}  // namespace ag
