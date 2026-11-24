// Linux I2C (/dev/i2c-N) register access, shared by the MAX30102 (0x57)
// and the MPU6050 (0x68) on I2C1 (GPIO2/GPIO3).
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ag {

class I2cBus {
 public:
  ~I2cBus();
  bool open(const char* dev = "/dev/i2c-1");
  bool ok() const { return fd_ >= 0; }
  bool writeReg(uint8_t addr, uint8_t reg, uint8_t value);
  bool readRegs(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len);
  bool readReg(uint8_t addr, uint8_t reg, uint8_t& value) { return readRegs(addr, reg, &value, 1); }
  uint32_t errors() const { return errors_; }

 private:
  int fd_ = -1;
  uint32_t errors_ = 0;
};

}  // namespace ag
