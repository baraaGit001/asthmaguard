// Vibration motor, buzzer and inhaler servo, all non-blocking.
//   Zero 2 W: buzzer GPIO23, vibration GPIO24 (each through a 2N2222; never
//   drive the motor straight from a GPIO), servo on hardware PWM GPIO12,
//   powered from the 5 V pin. PWM is switched off when idle (no jitter).
#pragma once
#include <stdint.h>

#include "config.h"
#include "hw/gpio.h"
#include "hw/pwm.h"
#include "state_machine.h"

namespace ag {

struct Step {
  uint8_t vib, buz;
  uint16_t ms;
};

class Actuators {
 public:
  explicit Actuators(const Config& cfg) : cfg_(cfg) {}
  // Returns a bitmask of what failed: 1 buzzer, 2 vibration, 4 servo PWM.
  int begin(const char* gpiochip, unsigned buzzer, unsigned vib, int pwm_chip, int pwm_channel);
  // Repeating background pattern from the state machine's alarm.
  void setAlarm(Alarm a);
  // One-shot patterns played on top of the alarm.
  void tick();            // short haptic tick (long-press reached 2 s)
  void testAlert();       // 3 s of the ALERT pattern
  // Puff: press, hold, release. Returns false if already moving.
  bool startPuff();
  void testServo() { startPuff(); }
  // Call every loop. Returns true once when a puff has finished.
  bool update(uint32_t now_ms);
  bool servoBusy() const { return servo_phase_ != 0; }

 private:
  void play(const Step* steps, uint8_t n, bool repeat, uint32_t now_ms);
  void out(bool vib, bool buz);

  const Config& cfg_;
  GpioLine buz_, vib_;
  ServoPwm servo_;
  Alarm alarm_ = Alarm::NONE;
  bool alarm_changed_ = false;
  // pattern player
  const Step* steps_ = nullptr;
  uint8_t n_ = 0, idx_ = 0;
  bool repeat_ = false;
  uint32_t step_end_ = 0;
  // one-shot overlay
  const Step* os_ = nullptr;
  uint8_t os_n_ = 0, os_idx_ = 0;
  uint32_t os_end_ = 0, test_until_ = 0;
  bool os_pending_ = false, test_pending_ = false;
  // servo
  uint8_t servo_phase_ = 0;  // 0 idle, 1 pressing, 2 releasing
  uint32_t servo_t_ = 0;
  bool puff_pending_ = false;
};

}  // namespace ag
