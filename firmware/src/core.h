// Glue for the pure logic: owns config, event log, clock, simulation,
// cough counter, detector and state machine, and runs the 1 Hz tick.
// main.cpp feeds it sensor readings; the host tests drive it directly.
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "config.h"
#include "detector.h"
#include "eventlog.h"
#include "sim.h"
#include "state_machine.h"
#include "types.h"

namespace ag {

struct SensorSample {      // raw readings for one second
  float hr = 0, spo2 = 0;
  bool ppg_valid = false;
  Activity activity = Activity::REST;
  uint8_t battery = 100;
};

class Core {
 public:
  Core() : det(cfg), coughs(cfg), sm(cfg, log, clock) {}

  void secondTick(uint32_t now_ms, const SensorSample& raw);
  void onButton(ButtonEvent e, uint32_t now_ms) { sm.onButton(e, now_ms); }
  void onArmRaise(uint32_t now_ms) { sm.onArmRaise(now_ms); }
  void onPuffDone(uint32_t now_ms) { sm.onPuffDone(now_ms); }
  void onCoughScore(float score, uint32_t now_ms) { coughs.onScore(score, now_ms); }
  void setTime(uint32_t epoch, uint32_t now_ms);

  // Handles a `cmd` write or a serial line. Writes a short reply.
  // Returns false for unknown / bad commands.
  bool command(const char* cmd, uint32_t now_ms, char* reply, size_t len);

  // Live JSON for the `live` characteristic (section 5.3).
  size_t liveJson(char* buf, size_t len) const;

  // One-shot requests for the actuator layer.
  bool takeTestAlert() { bool r = test_alert_; test_alert_ = false; return r; }
  bool takeTestServo() { bool r = test_servo_; test_servo_ = false; return r; }
  bool takeConfigChanged() { bool r = config_changed_; config_changed_ = false; return r; }

  Config cfg;
  EventLog log;
  Clock clock;
  Sim sim;
  Detector det;
  CoughCounter coughs;
  StateMachine sm;

  Signals sig;      // last signals after simulation overrides
  Rules rules;      // last rule evaluation
  uint8_t battery = 100;

 private:
  bool low_bat_sent_ = false;
  bool test_alert_ = false, test_servo_ = false, config_changed_ = false;
};

}  // namespace ag
