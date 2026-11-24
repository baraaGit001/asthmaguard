#include "eventlog.h"

#include <stdio.h>
#include <string.h>

namespace ag {

const char* toString(EventType t) {
  switch (t) {
    case EventType::SUSPECT: return "SUSPECT";
    case EventType::ATTACK_START: return "ATTACK_START";
    case EventType::FALSE_ALARM: return "FALSE_ALARM";
    case EventType::ARM_RAISED: return "ARM_RAISED";
    case EventType::DOSE: return "DOSE";
    case EventType::DOSE_BLOCKED: return "DOSE_BLOCKED";
    case EventType::IMPROVED: return "IMPROVED";
    case EventType::EMERGENCY: return "EMERGENCY";
    case EventType::MANUAL_SOS: return "MANUAL_SOS";
    case EventType::EMERGENCY_CANCELLED: return "EMERGENCY_CANCELLED";
    case EventType::RESOLVED: return "RESOLVED";
    case EventType::LOW_BATTERY: return "LOW_BATTERY";
    case EventType::BOOT: return "BOOT";
  }
  return "?";
}

static bool critical(EventType t) {
  return t == EventType::EMERGENCY || t == EventType::MANUAL_SOS || t == EventType::DOSE;
}

uint32_t EventLog::add(EventType type, uint32_t epoch, uint32_t up_s, const char* data_json) {
  if (n_ == kCap) {
    size_t victim = 0;
    for (size_t i = 0; i < n_; i++) {
      if (!critical(ev_[i].type)) { victim = i; break; }
    }
    removeAt(victim);
    dropped_++;
  }
  Event& e = ev_[n_++];
  e.id = next_id_++;
  e.t = epoch;
  e.up_s = up_s;
  e.type = type;
  const char* d = data_json && *data_json ? data_json : "{}";
  strncpy(e.data, d, sizeof(e.data) - 1);
  e.data[sizeof(e.data) - 1] = 0;
  if (strlen(d) >= sizeof(e.data)) strcpy(e.data, "{}");  // never send cut JSON
  return e.id;
}

void EventLog::removeAt(size_t i) {
  for (size_t j = i + 1; j < n_; j++) ev_[j - 1] = ev_[j];
  n_--;
}

bool EventLog::ack(uint32_t id) {
  for (size_t i = 0; i < n_; i++) {
    if (ev_[i].id == id) { removeAt(i); return true; }
  }
  return false;
}

const Event* EventLog::at(size_t i) const { return i < n_ ? &ev_[i] : nullptr; }

const Event* EventLog::find(uint32_t id) const {
  for (size_t i = 0; i < n_; i++) if (ev_[i].id == id) return &ev_[i];
  return nullptr;
}

void EventLog::backfillTime(uint32_t now_epoch, uint32_t now_up_s) {
  for (size_t i = 0; i < n_; i++) {
    if (ev_[i].t == 0 && now_up_s >= ev_[i].up_s) ev_[i].t = now_epoch - (now_up_s - ev_[i].up_s);
  }
}

size_t EventLog::toJson(const Event& e, char* buf, size_t len) {
  int n = snprintf(buf, len, "{\"id\":%lu,\"t\":%lu,\"up\":%lu,\"type\":\"%s\",\"data\":%s}",
                   (unsigned long)e.id, (unsigned long)e.t, (unsigned long)e.up_s,
                   toString(e.type), e.data);
  if (n < 0 || (size_t)n >= len) return 0;
  return (size_t)n;
}

}  // namespace ag
