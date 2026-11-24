#include "types.h"

#include <string.h>

namespace ag {

const char* toString(Activity a) {
  switch (a) {
    case Activity::REST: return "REST";
    case Activity::LIGHT: return "LIGHT";
    case Activity::EXERCISE: return "EXERCISE";
  }
  return "?";
}

const char* toString(State s) {
  switch (s) {
    case State::NORMAL: return "NORMAL";
    case State::SUSPECT: return "SUSPECT";
    case State::ALERT: return "ALERT";
    case State::WAIT_RAISE: return "WAIT_RAISE";
    case State::WAIT_CONFIRM: return "WAIT_CONFIRM";
    case State::DOSE: return "DOSE";
    case State::MONITOR: return "MONITOR";
    case State::RESOLVED: return "RESOLVED";
    case State::EMERGENCY: return "EMERGENCY";
  }
  return "?";
}

bool parseActivity(const char* s, Activity& out) {
  if (!strcmp(s, "REST")) { out = Activity::REST; return true; }
  if (!strcmp(s, "LIGHT")) { out = Activity::LIGHT; return true; }
  if (!strcmp(s, "EXERCISE")) { out = Activity::EXERCISE; return true; }
  return false;
}

}  // namespace ag
