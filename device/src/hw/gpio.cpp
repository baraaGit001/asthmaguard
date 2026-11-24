#include "hw/gpio.h"

#include <fcntl.h>
#include <linux/gpio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace ag {

GpioLine::~GpioLine() {
  if (fd_ >= 0) {
    if (have_last_) set(false);  // leave outputs off
    ::close(fd_);
  }
}

static int requestLine(const char* chip, unsigned offset, uint64_t flags, bool initial) {
  int cfd = ::open(chip, O_RDWR | O_CLOEXEC);
  if (cfd < 0) return -1;
  gpio_v2_line_request req;
  memset(&req, 0, sizeof req);
  req.offsets[0] = offset;
  req.num_lines = 1;
  strncpy(req.consumer, "asthmaguard", sizeof req.consumer - 1);
  req.config.flags = flags;
  if (flags & GPIO_V2_LINE_FLAG_OUTPUT) {
    req.config.num_attrs = 1;
    req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
    req.config.attrs[0].attr.values = initial ? 1 : 0;
    req.config.attrs[0].mask = 1;
  }
  int rc = ioctl(cfd, GPIO_V2_GET_LINE_IOCTL, &req);
  ::close(cfd);
  return rc < 0 ? -1 : req.fd;
}

bool GpioLine::openOutput(const char* chip, unsigned offset, bool initial) {
  fd_ = requestLine(chip, offset, GPIO_V2_LINE_FLAG_OUTPUT, initial);
  last_ = initial;
  have_last_ = fd_ >= 0;
  return fd_ >= 0;
}

bool GpioLine::openInputPullUp(const char* chip, unsigned offset) {
  fd_ = requestLine(chip, offset, GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_BIAS_PULL_UP, false);
  return fd_ >= 0;
}

void GpioLine::set(bool on) {
  if (fd_ < 0) return;
  if (have_last_ && last_ == on) return;  // avoid a syscall per loop
  gpio_v2_line_values v;
  v.bits = on ? 1 : 0;
  v.mask = 1;
  if (ioctl(fd_, GPIO_V2_LINE_SET_VALUES_IOCTL, &v) == 0) {
    last_ = on;
    have_last_ = true;
  }
}

bool GpioLine::get() const {
  if (fd_ < 0) return false;
  gpio_v2_line_values v;
  v.bits = 0;
  v.mask = 1;
  if (ioctl(fd_, GPIO_V2_LINE_GET_VALUES_IOCTL, &v) < 0) return false;
  return v.bits & 1;
}

}  // namespace ag
