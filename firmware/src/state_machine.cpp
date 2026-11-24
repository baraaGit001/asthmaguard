#include "state_machine.h"

#include <stdio.h>

namespace ag {

namespace {
const uint32_t kPuffTimeoutMs = 5000;  // DOSE gives up waiting for the servo
}

Alarm StateMachine::alarm() const {
  switch (state_) {
    case State::ALERT: return Alarm::ALERT;
    case State::WAIT_RAISE: return Alarm::RAISE;
    case State::WAIT_CONFIRM: return Alarm::CONFIRM;
    case State::EMERGENCY: return Alarm::EMERGENCY;
    case State::MONITOR: return emergency_active_ ? Alarm::EMERGENCY : Alarm::NONE;
    default: return Alarm::NONE;
  }
}

uint32_t StateMachine::emit(EventType t, uint32_t now_ms, const char* extra) {
  char data[112];
  if (extra && *extra) {
    snprintf(data, sizeof(data), "{\"hr\":%d,\"spo2\":%d,\"v\":%d,\"c60\":%d,%s}", (int)(last_.hr + 0.5f),
             (int)(last_.spo2 + 0.5f), last_.ppg_valid ? 1 : 0, last_.cough_60s, extra);
  } else {
    snprintf(data, sizeof(data), "{\"hr\":%d,\"spo2\":%d,\"v\":%d,\"c60\":%d}", (int)(last_.hr + 0.5f),
             (int)(last_.spo2 + 0.5f), last_.ppg_valid ? 1 : 0, last_.cough_60s);
  }
  return log_.add(t, clock_.epochAt(now_ms), now_ms / 1000u, data);
}

void StateMachine::enter(State s, uint32_t now_ms) {
  state_ = s;
  entered_ms_ = now_ms;
  if (s == State::MONITOR) {
    improved_run_s_ = 0;
    improved_sent_ = false;
  }
}

void StateMachine::startEpisode(uint32_t now_ms, const char* reason) {
  if (episode_) return;
  episode_ = true;
  doses_ep_ = 0;
  emergency_sent_ = false;
  emergency_active_ = false;
  char extra[40];
  snprintf(extra, sizeof(extra), "\"reason\":\"%s\"", reason);
  emit(EventType::ATTACK_START, now_ms, extra);
}

void StateMachine::endEpisode() {
  episode_ = false;
  emergency_active_ = false;
  emergency_sent_ = false;
  attack_run_s_ = 0;
  nosign_s_ = 0;
}

bool StateMachine::emergencyRecentlySent(uint32_t now_ms) const {
  return emergency_sent_ && now_ms - emergency_ms_ < cfg_.reescalate_s * 1000u;
}

void StateMachine::sendEmergency(EventType type, uint32_t now_ms, const char* reason) {
  char extra[40];
  snprintf(extra, sizeof(extra), "\"reason\":\"%s\"", reason);
  emergency_id_ = emit(type, now_ms, extra);
  emergency_sent_ = true;
  emergency_active_ = true;
  emergency_ms_ = now_ms;
}

bool StateMachine::doseAllowed(uint32_t now_ms) const { return *doseBlockReason(now_ms) == 0; }

const char* StateMachine::doseBlockReason(uint32_t now_ms) const {
  if (!cfg_.doseLimitsSet()) return "limits_not_set";
  if (doses_ep_ >= cfg_.max_doses) return "episode_limit";
  if (has_last_dose_ && now_ms - last_dose_ms_ < cfg_.dose_interval_s * 1000u) return "interval";
  return "";
}

void StateMachine::tick(uint32_t now_ms, const Rules& r, const Signals& s) {
  last_ = s;
  attack_run_s_ = r.attack ? attack_run_s_ + 1 : 0;
  nosign_s_ = r.any_sign ? 0 : nosign_s_ + 1;

  // SPO2_CRITICAL at any time -> attack alert + emergency (4.2).
  if (r.spo2_critical && state_ != State::EMERGENCY && !emergencyRecentlySent(now_ms)) {
    switch (state_) {
      case State::NORMAL:
      case State::SUSPECT:
      case State::RESOLVED:
        startEpisode(now_ms, "spo2_critical");
        enter(State::ALERT, now_ms);
        sendEmergency(EventType::EMERGENCY, now_ms, "spo2_critical");
        return;
      case State::MONITOR:
        enter(State::EMERGENCY, now_ms);
        sendEmergency(EventType::EMERGENCY, now_ms, "spo2_critical");
        return;
      default:  // ALERT / WAIT_* / DOSE: escalate but let the dosing flow go on
        sendEmergency(EventType::EMERGENCY, now_ms, "spo2_critical");
        break;
    }
  }

  uint32_t in_state_ms = now_ms - entered_ms_;
  switch (state_) {
    case State::NORMAL:
      if (r.any_sign) {
        enter(State::SUSPECT, now_ms);
        char extra[48];
        snprintf(extra, sizeof(extra), "\"cough\":%d,\"hrs\":%d,\"spo2s\":%d", r.cough, r.hr, r.spo2);
        emit(EventType::SUSPECT, now_ms, extra);
      }
      break;

    case State::SUSPECT:
      if (attack_run_s_ >= cfg_.attack_hold_s) {
        startEpisode(now_ms, "attack_condition");
        enter(State::ALERT, now_ms);
      } else if (nosign_s_ >= cfg_.suspect_clear_s) {
        enter(State::NORMAL, now_ms);
      }
      break;

    case State::ALERT:
      if (in_state_ms >= cfg_.cancel_window_s * 1000u) {
        enter(State::WAIT_RAISE, now_ms);
        raise_deadline_ms_ = now_ms + cfg_.raise_timeout_s * 1000u;
      }
      break;

    case State::WAIT_RAISE:
      if ((int32_t)(now_ms - raise_deadline_ms_) >= 0) {
        enter(State::EMERGENCY, now_ms);
        sendEmergency(EventType::EMERGENCY, now_ms, "no_response");
      }
      break;

    case State::WAIT_CONFIRM:
      if (in_state_ms >= cfg_.confirm_timeout_s * 1000u) enter(State::WAIT_RAISE, now_ms);
      break;

    case State::DOSE:
      if (in_state_ms >= cfg_.servo_hold_ms + kPuffTimeoutMs) {
        // Servo never reported back. Do not count a dose we cannot confirm.
        emit(EventType::DOSE_BLOCKED, now_ms, "\"reason\":\"servo_timeout\"");
        enter(State::MONITOR, now_ms);
      }
      break;

    case State::MONITOR:
      improved_run_s_ = r.improved ? improved_run_s_ + 1 : 0;
      if (improved_run_s_ == 1 && !improved_sent_) {
        improved_sent_ = true;
        emit(EventType::IMPROVED, now_ms);
      }
      if (improved_run_s_ >= cfg_.improve_hold_s) {
        enter(State::RESOLVED, now_ms);
        char extra[24];
        snprintf(extra, sizeof(extra), "\"doses\":%d", doses_ep_);
        emit(EventType::RESOLVED, now_ms, extra);
        endEpisode();
      } else if (in_state_ms >= cfg_.monitor_timeout_s * 1000u) {
        if (doseAllowed(now_ms)) {
          enter(State::ALERT, now_ms);  // offer another puff
        } else if (!emergencyRecentlySent(now_ms)) {
          enter(State::EMERGENCY, now_ms);
          sendEmergency(EventType::EMERGENCY, now_ms, "not_improved");
        } else {
          enter(State::MONITOR, now_ms);  // help already called; keep watching
        }
      }
      break;

    case State::RESOLVED:
      enter(State::NORMAL, now_ms);
      break;

    case State::EMERGENCY:
      break;  // leaves on phone ACK (onAck) or a cancel long-press
  }
}

void StateMachine::onButton(ButtonEvent e, uint32_t now_ms) {
  switch (e) {
    case ButtonEvent::SOS:
      // Manual SOS works from anywhere, also when an emergency is already on
      // (a second SOS re-sends it).
      startEpisode(now_ms, "manual_sos");
      want_puff_ = false;
      enter(State::EMERGENCY, now_ms);
      sendEmergency(EventType::MANUAL_SOS, now_ms, "button");
      break;

    case ButtonEvent::LONG:
      if (state_ == State::ALERT && now_ms - entered_ms_ < cfg_.cancel_window_s * 1000u &&
          !emergency_active_) {
        emit(EventType::FALSE_ALARM, now_ms);
        endEpisode();
        enter(State::NORMAL, now_ms);
      } else if (state_ == State::EMERGENCY) {
        emit(EventType::EMERGENCY_CANCELLED, now_ms);
        emergency_active_ = false;
        enter(State::MONITOR, now_ms);
      }
      break;

    case ButtonEvent::SHORT:
      if (state_ == State::WAIT_CONFIRM) {
        if (doseAllowed(now_ms)) {
          enter(State::DOSE, now_ms);
          want_puff_ = true;
        } else {
          char extra[40];
          snprintf(extra, sizeof(extra), "\"reason\":\"%s\"", doseBlockReason(now_ms));
          emit(EventType::DOSE_BLOCKED, now_ms, extra);
          enter(State::MONITOR, now_ms);
        }
      }
      break;

    case ButtonEvent::NONE:
      break;
  }
}

void StateMachine::onArmRaise(uint32_t now_ms) {
  if (state_ != State::WAIT_RAISE) return;
  emit(EventType::ARM_RAISED, now_ms);
  enter(State::WAIT_CONFIRM, now_ms);
}

void StateMachine::onPuffDone(uint32_t now_ms) {
  if (state_ != State::DOSE) return;
  doses_ep_++;
  has_last_dose_ = true;
  last_dose_ms_ = now_ms;
  char extra[32];
  snprintf(extra, sizeof(extra), "\"n\":%d,\"max\":%d", doses_ep_, cfg_.max_doses);
  emit(EventType::DOSE, now_ms, extra);
  enter(State::MONITOR, now_ms);
}

void StateMachine::onAck(uint32_t event_id, uint32_t now_ms) {
  log_.ack(event_id);
  if (state_ == State::EMERGENCY && event_id == emergency_id_) {
    // Phone confirmed the SMS went out. Alerts continue until improved.
    enter(State::MONITOR, now_ms);
  }
}

}  // namespace ag
