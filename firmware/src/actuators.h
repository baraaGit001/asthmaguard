// Vibration motor, buzzer and inhaler servo, all non-blocking.
//   Vibration GP14 and buzzer GP13 drive 2N2222 transistors (never the
//   motor straight from a GPIO). Servo signal GP15, powered from the 5 V
//   boost. The servo is detached when idle (no jitter, less current).
#pragma once
#include <stdint.h>

#include "config.h"
#include "state_machine.h"

namespace ag {

struct Step {
  uint8_t vib, buz;
  uint16_t ms;
};

class Actuators {
 public:
  explicit Actuators(const Config& cfg) : cfg_(cfg) {}
  void begin(uint8_t pin_buzzer, uint8_t pin_vib, uint8_t pin_servo);
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
  uint8_t pb_ = 13, pv_ = 14, ps_ = 15;
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
