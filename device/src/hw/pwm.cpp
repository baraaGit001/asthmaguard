#include "hw/pwm.h"

#include <fcntl.h>
#include <time.h>
#include <unistd.h>

namespace ag {

namespace {
const long kPeriodNs = 20000000;  // 50 Hz
bool writeTo(const std::string& path, const std::string& v) {
  int fd = ::open(path.c_str(), O_WRONLY | O_CLOEXEC);
  if (fd < 0) return false;
  bool ok = ::write(fd, v.data(), v.size()) == (ssize_t)v.size();
  ::close(fd);
  return ok;
}
}  // namespace

ServoPwm::~ServoPwm() { disable(); }

bool ServoPwm::writeFile(const std::string& name, const std::string& value) {
  return writeTo(base_ + "/" + name, value);
}

bool ServoPwm::open(int chip, int channel) {
  std::string chipdir = "/sys/class/pwm/pwmchip" + std::to_string(chip);
  base_ = chipdir + "/pwm" + std::to_string(channel);
  if (access(base_.c_str(), F_OK) != 0) {
    writeTo(chipdir + "/export", std::to_string(channel));
    // udev needs a moment to fix permissions on the new pwmM directory
    for (int i = 0; i < 50 && access((base_ + "/period").c_str(), W_OK) != 0; i++) {
      timespec ts = {0, 20000000};
      nanosleep(&ts, nullptr);
    }
  }
  writeFile("enable", "0");
  ok_ = writeFile("period", std::to_string(kPeriodNs));
  return ok_;
}

void ServoPwm::writeAngle(int deg) {
  if (!ok_) return;
  if (deg < 0) deg = 0;
  if (deg > 180) deg = 180;
  long us = 500 + (long)deg * 2000 / 180;
  writeFile("duty_cycle", std::to_string(us * 1000));
  if (!enabled_) enabled_ = writeFile("enable", "1");
}

void ServoPwm::disable() {
  if (!ok_ || !enabled_) return;
  writeFile("enable", "0");
  enabled_ = false;
}

}  // namespace ag
