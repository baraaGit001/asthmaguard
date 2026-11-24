// Ring buffer of events (section 5.3). An event stays until the phone ACKs
// it, so nothing is lost while the phone is away. If the buffer fills, the
// oldest *non-critical* event is dropped first; EMERGENCY / MANUAL_SOS / DOSE
// are dropped only when nothing else is left.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace ag {

enum class EventType : uint8_t {
  SUSPECT, ATTACK_START, FALSE_ALARM, ARM_RAISED, DOSE, DOSE_BLOCKED,
  IMPROVED, EMERGENCY, MANUAL_SOS, EMERGENCY_CANCELLED, RESOLVED,
  LOW_BATTERY, BOOT,
};
const char* toString(EventType t);
bool parseEventType(const char* s, EventType& out);

struct Event {
  uint32_t id = 0;
  uint32_t t = 0;        // epoch seconds (0 if the phone has not set the time yet)
  uint32_t up_s = 0;     // device uptime seconds (lets the phone fix t when 0)
  EventType type = EventType::BOOT;
  char data[112] = {0};  // JSON object text, e.g. {"spo2":91,"hr":120}
};

class EventLog {
 public:
  static const uint8_t kCap = 48;

  // Adds an event, returns its id. `data_json` may be null ("{}").
  uint32_t add(EventType type, uint32_t epoch, uint32_t up_s, const char* data_json);
  bool ack(uint32_t id);                 // removes it; false if unknown
  // Re-insert an event loaded from disk (keeps its id). False if full/duplicate.
  bool restore(const Event& e);
  // Changes whenever the pending set changes (for "save if changed").
  uint32_t version() const { return version_; }
  size_t pending() const { return n_; }
  const Event* at(size_t i) const;       // i-th oldest pending event
  const Event* find(uint32_t id) const;
  bool isPending(uint32_t id) const { return find(id) != nullptr; }
  // Fill in epoch time for events created before the clock was set.
  void backfillTime(uint32_t now_epoch, uint32_t now_up_s);
  uint32_t nextId() const { return next_id_; }
  void setNextId(uint32_t id) { next_id_ = id; }
  uint32_t dropped() const { return dropped_; }

  // JSON for the `event` characteristic. Returns length (0 if too small).
  static size_t toJson(const Event& e, char* buf, size_t len);

 private:
  void removeAt(size_t i);
  Event ev_[kCap];
  size_t n_ = 0;
  uint32_t next_id_ = 1;
  uint32_t dropped_ = 0;
  uint32_t version_ = 0;
};

}  // namespace ag
