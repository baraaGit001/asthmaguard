#include "i2c.h"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace ag {

I2cBus::~I2cBus() {
  if (fd_ >= 0) ::close(fd_);
}

bool I2cBus::open(const char* dev) {
  fd_ = ::open(dev, O_RDWR | O_CLOEXEC);
  return fd_ >= 0;
}

bool I2cBus::writeReg(uint8_t addr, uint8_t reg, uint8_t value) {
  if (fd_ < 0) return false;
  uint8_t buf[2] = {reg, value};
  i2c_msg msg = {addr, 0, 2, buf};
  i2c_rdwr_ioctl_data data = {&msg, 1};
  if (ioctl(fd_, I2C_RDWR, &data) != 1) {
    errors_++;
    return false;
  }
  return true;
}

// Register write + repeated-start read in one transaction.
bool I2cBus::readRegs(uint8_t addr, uint8_t reg, uint8_t* buf, size_t len) {
  if (fd_ < 0) return false;
  i2c_msg msgs[2] = {
      {addr, 0, 1, &reg},
      {addr, I2C_M_RD, (uint16_t)len, buf},
  };
  i2c_rdwr_ioctl_data data = {msgs, 2};
  if (ioctl(fd_, I2C_RDWR, &data) != 2) {
    errors_++;
    return false;
  }
  return true;
}

}  // namespace ag
