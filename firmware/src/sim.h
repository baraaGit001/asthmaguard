// Simulation mode (section 5.4). Overrides any signal so the real detector
// and state machine run on fake values. Commands (BLE `cmd` or serial):
//   SIM:ON  SIM:OFF  SIM:clear
//   SIM:hr=125  SIM:spo2=91  SIM:valid=1  SIM:cough=6  SIM:act=REST
//   SIM:raise  SIM:btn=short|long|sos  SIM:bat=10
// Setting hr or spo2 also makes the PPG reading valid unless SIM:valid=0.
#pragma once
#include <stdint.h>

#include "types.h"

namespace ag {

struct SimActions {
  bool raise = false;
  ButtonEvent button = ButtonEvent::NONE;
};

class Sim {
 public:
  // Handles a command string (with or without the "SIM:" prefix).
  // Returns false if the command is not understood.
  bool command(const char* cmd, SimActions& actions);
  bool enabled() const { return on_; }
  // Applies the overrides (only when enabled).
  void apply(Signals& s) const;
  // Battery override; returns true and sets pct when active.
  bool battery(uint8_t& pct) const;
  void describe(char* buf, unsigned len) const;

 private:
  bool on_ = false;
  bool has_hr_ = false, has_spo2_ = false, has_valid_ = false, has_cough_ = false,
       has_act_ = false, has_bat_ = false;
  float hr_ = 0, spo2_ = 0;
  bool valid_ = true;
  uint8_t cough_ = 0, bat_ = 100;
  Activity act_ = Activity::REST;
};

}  // namespace ag
