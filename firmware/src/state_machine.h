// Attack state machine (section 4.3). Pure logic: the caller feeds it the
// 1 Hz detector output, button gestures, arm-raise detections, the end of a
// puff and phone ACKs, and reads back the state, the alarm pattern to play
// and whether a puff should be delivered.
#pragma once
#include <stdint.h>

#include "config.h"
#include "detector.h"
#include "eventlog.h"
#include "types.h"

namespace ag {

// Wall clock: the Pico has no RTC, the phone sets the time on every connect.
class Clock {
 public:
  void set(uint32_t epoch, uint32_t now_ms) { epoch_ = epoch; at_ms_ = now_ms; valid_ = true; }
  bool valid() const { return valid_; }
  uint32_t epochAt(uint32_t now_ms) const {
    return valid_ ? epoch_ + (now_ms - at_ms_) / 1000u : 0;
  }

 private:
  uint32_t epoch_ = 0, at_ms_ = 0;
  bool valid_ = false;
};

enum class Alarm : uint8_t {
  NONE,
  ALERT,      // 500 ms on / 500 ms off vibration + beeps
  RAISE,      // prompt: raise your arm
  CONFIRM,    // prompt: press the button to puff
  EMERGENCY,  // continuous alarm until improved
};

class StateMachine {
 public:
  StateMachine(const Config& cfg, EventLog& log, const Clock& clock)
      : cfg_(cfg), log_(log), clock_(clock) {}

  void tick(uint32_t now_ms, const Rules& r, const Signals& s);  // 1 Hz
  void onButton(ButtonEvent e, uint32_t now_ms);
  void onArmRaise(uint32_t now_ms);
  void onPuffDone(uint32_t now_ms);
  // Phone ACKed an event (removes it from the log).
  void onAck(uint32_t event_id, uint32_t now_ms);

  State state() const { return state_; }
  Alarm alarm() const;
  bool takePuffRequest() { bool p = want_puff_; want_puff_ = false; return p; }
  bool puffInProgress() const { return state_ == State::DOSE; }
  bool episodeActive() const { return episode_; }
  bool emergencyActive() const { return emergency_active_; }
  uint32_t emergencyEventId() const { return emergency_id_; }
  uint8_t dosesThisEpisode() const { return doses_ep_; }
  bool doseAllowed(uint32_t now_ms) const;
  // Why a dose is not allowed right now ("" if allowed).
  const char* doseBlockReason(uint32_t now_ms) const;
  uint32_t secondsInState(uint32_t now_ms) const { return (now_ms - entered_ms_) / 1000u; }

 private:
  void enter(State s, uint32_t now_ms);
  void startEpisode(uint32_t now_ms, const char* reason);
  void endEpisode();
  void sendEmergency(EventType type, uint32_t now_ms, const char* reason);
  bool emergencyRecentlySent(uint32_t now_ms) const;
  uint32_t emit(EventType t, uint32_t now_ms, const char* extra = nullptr);

  const Config& cfg_;
  EventLog& log_;
  const Clock& clock_;

  State state_ = State::NORMAL;
  uint32_t entered_ms_ = 0;
  Signals last_{};

  uint16_t attack_run_s_ = 0;
  uint16_t nosign_s_ = 0;
  uint16_t improved_run_s_ = 0;
  bool improved_sent_ = false;

  bool episode_ = false;
  uint8_t doses_ep_ = 0;
  bool has_last_dose_ = false;
  uint32_t last_dose_ms_ = 0;
  uint32_t raise_deadline_ms_ = 0;

  bool emergency_active_ = false;
  bool emergency_sent_ = false;
  uint32_t emergency_ms_ = 0;
  uint32_t emergency_id_ = 0;

  bool want_puff_ = false;
};

}  // namespace ag
