// Configurable thresholds (section 4). Every number is a default that the
// doctor area of the app can change through the BLE `config` characteristic.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ag {

struct Config {
  // ---- cough events (4.1)
  float cough_score_min = 0.80f;   // model score for a cough event
  uint16_t cough_gap_ms = 300;     // min gap between two events

  // ---- rules (4.2)
  uint8_t cough_sign = 4;          // COUGH_SIGN: cough_60s >= this
  uint8_t hr_rise = 20;            // HR_SIGN: hr >= baseline + this
  uint8_t hr_abs = 110;            //          or hr >= this
  uint8_t spo2_sign = 94;          // SPO2_SIGN: spo2 <= this
  uint8_t spo2_crit = 92;          // SPO2_CRITICAL: spo2 < this ...
  uint16_t spo2_crit_s = 20;       //                ... for this many seconds

  // ---- state machine timing (4.3), seconds
  uint16_t attack_hold_s = 30;     // ATTACK_CONDITION held -> ALERT
  uint16_t suspect_clear_s = 60;   // SUSPECT -> NORMAL after no sign
  uint16_t cancel_window_s = 15;   // ALERT: long-press cancels within this
  uint16_t raise_timeout_s = 60;   // WAIT_RAISE -> EMERGENCY
  uint16_t confirm_timeout_s = 10; // WAIT_CONFIRM -> WAIT_RAISE
  uint16_t improve_hold_s = 180;   // MONITOR -> RESOLVED
  uint16_t monitor_timeout_s = 600;// MONITOR: not improved after this
  uint16_t reescalate_s = 600;     // min gap between two EMERGENCY sends in one episode
  uint16_t emergency_retry_s = 10; // resend EMERGENCY while not ACKed

  // ---- "improved" (4.3 notes)
  uint8_t improve_spo2 = 94;       // valid spo2 >= this
  uint8_t improve_hr_margin = 10;  // hr <= baseline + this
  uint8_t improve_cough_max = 1;   // cough_60s <= this

  // ---- personal baseline (6.4). Defaults until calibration.
  float baseline_hr = 75;
  float baseline_spo2 = 97;
  uint32_t calibrated_at = 0;      // epoch seconds, 0 = not calibrated

  // ---- dosing (set by the doctor; 0 = not set -> dosing disabled)
  uint8_t max_doses = 0;           // per episode
  uint32_t dose_interval_s = 0;    // min time between two puffs

  // ---- servo
  uint8_t servo_rest = 0;          // degrees
  uint8_t servo_press = 90;        // degrees
  uint16_t servo_hold_ms = 500;

  // ---- arm raise (IMU)
  uint8_t raise_deg = 60;          // pitch change
  uint16_t raise_within_ms = 1500;
  uint16_t raise_hold_ms = 1000;
  uint8_t raise_invert = 0;        // 1 if the IMU is mounted so a raise lowers pitch

  // ---- button
  uint16_t long_press_ms = 2000;
  uint16_t sos_press_ms = 5000;

  // ---- battery
  uint8_t low_battery_pct = 15;

  bool doseLimitsSet() const { return max_doses > 0 && dose_interval_s > 0; }

  // Serialise to compact JSON. Returns bytes written (0 if buf too small).
  size_t toJson(char* buf, size_t len) const;
  // Partial update from JSON: only keys present change. All values are
  // range-checked first; on any error nothing changes and `err` says why.
  bool fromJson(const char* json, size_t len, const char** err);
};

}  // namespace ag
