// Servo on hardware PWM through sysfs (/sys/class/pwm/pwmchipN/pwmM).
// config.txt: dtoverlay=pwm,pin=12,func=4  -> GPIO12 = PWM0 channel 0.
// Hardware PWM gives a jitter-free 50 Hz pulse (software PWM would shake the servo).
#pragma once
#include <string>

namespace ag {

class ServoPwm {
 public:
  ~ServoPwm();
  bool open(int chip = 0, int channel = 0);
  bool ok() const { return ok_; }
  void writeAngle(int deg);  // 0..180 -> 500..2500 us, enables output
  void disable();            // stop pulses (servo relaxes, no jitter, less current)
 private:
  bool writeFile(const std::string& name, const std::string& value);
  std::string base_;
  bool ok_ = false, enabled_ = false;
};

}  // namespace ag
