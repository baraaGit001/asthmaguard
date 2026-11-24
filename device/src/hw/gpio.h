// GPIO lines through the Linux GPIO character device (v2 uAPI, kernel >= 5.10).
// No libgpiod dependency. On the Zero 2 W the header GPIOs are /dev/gpiochip0.
#pragma once
#include <stdint.h>

namespace ag {

class GpioLine {
 public:
  ~GpioLine();
  GpioLine() = default;
  GpioLine(const GpioLine&) = delete;
  GpioLine& operator=(const GpioLine&) = delete;

  bool openOutput(const char* chip, unsigned offset, bool initial = false);
  bool openInputPullUp(const char* chip, unsigned offset);
  bool ok() const { return fd_ >= 0; }
  void set(bool on);
  bool get() const;  // false on error
 private:
  int fd_ = -1;
  bool last_ = false, have_last_ = false;
};

}  // namespace ag
