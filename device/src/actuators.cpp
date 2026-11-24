#include "actuators.h"

namespace ag {

namespace {
#define N(a) (uint8_t)(sizeof(a) / sizeof(a[0]))
// ALERT: vibration 500 ms on / 500 ms off plus buzzer beeps (section 4.3)
const Step kAlert[] = {{1, 1, 500}, {0, 0, 500}};
// RAISE: "raise your arm" - two buzzes and a short beep, then a pause
const Step kRaise[] = {{1, 0, 200}, {0, 0, 150}, {1, 1, 200}, {0, 0, 1450}};
// CONFIRM: "press to puff" - quick triple tap
const Step kConfirm[] = {{1, 1, 90}, {0, 0, 90}, {1, 1, 90}, {0, 0, 90}, {1, 1, 90}, {0, 0, 550}};
// EMERGENCY: fast continuous alarm
const Step kEmergency[] = {{1, 1, 250}, {0, 0, 250}};
const Step kTick[] = {{1, 0, 80}, {0, 0, 40}};
const uint32_t kServoSettleMs = 400;
}  // namespace

int Actuators::begin(const char* gpiochip, unsigned buzzer, unsigned vib, int pwm_chip, int pwm_channel) {
  int fail = 0;
  if (!buz_.openOutput(gpiochip, buzzer, false)) fail |= 1;
  if (!vib_.openOutput(gpiochip, vib, false)) fail |= 2;
  if (!servo_.open(pwm_chip, pwm_channel)) fail |= 4;
  return fail;
}

void Actuators::out(bool vib, bool buz) {
  vib_.set(vib);
  buz_.set(buz);
}

void Actuators::setAlarm(Alarm a) {
  if (a == alarm_) return;
  alarm_ = a;
  alarm_changed_ = true;
}

void Actuators::tick() { os_pending_ = true; }
void Actuators::testAlert() { test_pending_ = true; }

bool Actuators::startPuff() {
  // No servo = no puff. Never report a dose that could not be delivered.
  if (!servo_.ok() || servo_phase_ != 0 || puff_pending_) return false;
  puff_pending_ = true;
  return true;
}

void Actuators::play(const Step* steps, uint8_t n, bool repeat, uint32_t now_ms) {
  steps_ = steps;
  n_ = n;
  repeat_ = repeat;
  idx_ = 0;
  step_end_ = now_ms + (steps ? steps[0].ms : 0);
}

bool Actuators::update(uint32_t now_ms) {
  bool done = false;

  // ---- servo sequence
  if (puff_pending_) {
    puff_pending_ = false;
    servo_.writeAngle(cfg_.servo_press);
    servo_phase_ = 1;
    servo_t_ = now_ms;
  } else if (servo_phase_ == 1 && now_ms - servo_t_ >= cfg_.servo_hold_ms) {
    servo_.writeAngle(cfg_.servo_rest);
    servo_phase_ = 2;
    servo_t_ = now_ms;
  } else if (servo_phase_ == 2 && now_ms - servo_t_ >= kServoSettleMs) {
    servo_.disable();
    servo_phase_ = 0;
    done = true;
  }

  // ---- background alarm pattern
  if (alarm_changed_) {
    alarm_changed_ = false;
    switch (alarm_) {
      case Alarm::NONE: play(nullptr, 0, false, now_ms); break;
      case Alarm::ALERT: play(kAlert, N(kAlert), true, now_ms); break;
      case Alarm::RAISE: play(kRaise, N(kRaise), true, now_ms); break;
      case Alarm::CONFIRM: play(kConfirm, N(kConfirm), true, now_ms); break;
      case Alarm::EMERGENCY: play(kEmergency, N(kEmergency), true, now_ms); break;
    }
  }
  if (steps_ && (int32_t)(now_ms - step_end_) >= 0) {
    idx_++;
    if (idx_ >= n_) {
      if (repeat_) idx_ = 0;
      else steps_ = nullptr;
    }
    if (steps_) step_end_ = now_ms + steps_[idx_].ms;
  }

  // ---- one-shot overlays
  if (test_pending_) {
    test_pending_ = false;
    test_until_ = now_ms + 3000;
  }
  if (os_pending_) {
    os_pending_ = false;
    os_ = kTick;
    os_n_ = N(kTick);
    os_idx_ = 0;
    os_end_ = now_ms + kTick[0].ms;
  }
  if (os_ && (int32_t)(now_ms - os_end_) >= 0) {
    if (++os_idx_ >= os_n_) os_ = nullptr;
    else os_end_ = now_ms + os_[os_idx_].ms;
  }

  // ---- drive the pins (priority: puff silence > tick > test > alarm)
  if (servo_phase_ != 0) {
    out(false, false);  // keep the arm still while the puff is delivered
  } else if (os_) {
    out(os_[os_idx_].vib, os_[os_idx_].buz);
  } else if ((int32_t)(test_until_ - now_ms) > 0) {
    bool on = ((now_ms / 500) % 2) == 0;
    out(on, on);
  } else if (steps_) {
    out(steps_[idx_].vib, steps_[idx_].buz);
  } else {
    out(false, false);
  }
  return done;
}

}  // namespace ag
