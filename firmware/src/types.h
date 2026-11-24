// Shared types for the AsthmaGuard firmware.
// Everything in detector / state_machine / eventlog / sim is plain C++ with no
// Arduino dependency, so the same code runs in the host unit tests (pio test -e native).
#pragma once
#include <stdint.h>

namespace ag {

enum class Activity : uint8_t { REST = 0, LIGHT = 1, EXERCISE = 2 };

enum class State : uint8_t {
  NORMAL = 0,
  SUSPECT,
  ALERT,
  WAIT_RAISE,
  WAIT_CONFIRM,
  DOSE,
  MONITOR,
  RESOLVED,
  EMERGENCY,
};

// Values the detector sees once per second (section 4.1).
struct Signals {
  float hr = 0;          // beats per minute
  float spo2 = 0;        // percent
  bool ppg_valid = false;
  uint8_t cough_60s = 0;
  Activity activity = Activity::REST;
};

// Button gestures (produced by Button, or by simulation).
enum class ButtonEvent : uint8_t {
  NONE = 0,
  SHORT,       // released before 2 s
  LONG,        // released after 2 s but before 5 s (cancel / false alarm)
  SOS,         // still held at 5 s (fires while held)
};

const char* toString(Activity a);
const char* toString(State s);
bool parseActivity(const char* s, Activity& out);

}  // namespace ag
