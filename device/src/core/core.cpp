#include "core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ag {

void Core::secondTick(uint32_t now_ms, const SensorSample& raw) {
  Signals s;
  s.hr = raw.hr;
  s.spo2 = raw.spo2;
  s.ppg_valid = raw.ppg_valid;
  s.activity = raw.activity;
  s.cough_60s = coughs.count60(now_ms);
  sim.apply(s);
  sig = s;

  uint8_t bat = raw.battery;
  sim.battery(bat);
  battery = bat;
  if (bat == kBatteryUnknown) {
    // no battery reading: never warn
  } else if (bat <= cfg.low_battery_pct && !low_bat_sent_) {
    char d[16];
    snprintf(d, sizeof(d), "{\"bat\":%d}", bat);
    log.add(EventType::LOW_BATTERY, clock.epochAt(now_ms), now_ms / 1000u, d);
    low_bat_sent_ = true;
  } else if (bat >= cfg.low_battery_pct + 5) {
    low_bat_sent_ = false;  // hysteresis: warn again after a recharge
  }

  rules = det.update(s);
  sm.tick(now_ms, rules, s);
}

void Core::setTime(uint32_t epoch, uint32_t now_ms) {
  clock.set(epoch, now_ms);
  log.backfillTime(epoch, now_ms / 1000u);
}

bool Core::command(const char* cmd, uint32_t now_ms, char* reply, size_t len) {
  if (!strcmp(cmd, "TEST_ALERT")) { test_alert_ = true; snprintf(reply, len, "OK alert"); return true; }
  if (!strcmp(cmd, "TEST_SERVO")) {
    // Never move the servo while a real dose could be running.
    if (sm.state() == State::DOSE) { snprintf(reply, len, "ERR busy"); return false; }
    test_servo_ = true;
    snprintf(reply, len, "OK servo");
    return true;
  }
  if (!strncmp(cmd, "ACK:", 4)) {
    char* end = nullptr;
    unsigned long id = strtoul(cmd + 4, &end, 10);
    if (end == cmd + 4 || *end) { snprintf(reply, len, "ERR id"); return false; }
    bool known = log.isPending((uint32_t)id);
    sm.onAck((uint32_t)id, now_ms);
    snprintf(reply, len, known ? "OK ack %lu" : "OK ack %lu (unknown)", id);
    return true;
  }
  if (!strncmp(cmd, "TIME:", 5)) {
    char* end = nullptr;
    unsigned long t = strtoul(cmd + 5, &end, 10);
    if (end == cmd + 5 || *end || t < 1600000000UL) { snprintf(reply, len, "ERR time"); return false; }
    setTime((uint32_t)t, now_ms);
    snprintf(reply, len, "OK time");
    return true;
  }
  if (!strncmp(cmd, "CONFIG:", 7)) {
    const char* err = nullptr;
    if (!cfg.fromJson(cmd + 7, strlen(cmd + 7), &err)) { snprintf(reply, len, "ERR %s", err); return false; }
    config_changed_ = true;
    snprintf(reply, len, "OK config");
    return true;
  }
  if (!strncmp(cmd, "SIM:", 4)) {
    SimActions a;
    if (!sim.command(cmd, a)) { snprintf(reply, len, "ERR sim"); return false; }
    if (a.raise) sm.onArmRaise(now_ms);
    if (a.button != ButtonEvent::NONE) sm.onButton(a.button, now_ms);
    sim.describe(reply, (unsigned)len);
    return true;
  }
  snprintf(reply, len, "ERR unknown");
  return false;
}

size_t Core::liveJson(char* buf, size_t len) const {
  int n = snprintf(buf, len,
                   "{\"hr\":%d,\"spo2\":%d,\"v\":%d,\"c60\":%d,\"act\":\"%s\",\"st\":\"%s\",\"bat\":%d,"
                   "\"sim\":%d,\"pend\":%u,\"dose\":%d}",
                   (int)(sig.hr + 0.5f), (int)(sig.spo2 + 0.5f), sig.ppg_valid ? 1 : 0, sig.cough_60s,
                   toString(sig.activity), toString(sm.state()), battery == kBatteryUnknown ? -1 : (int)battery,
                   sim.enabled() ? 1 : 0,
                   (unsigned)log.pending(), cfg.doseLimitsSet() ? 1 : 0);
  if (n < 0 || (size_t)n >= len) return 0;
  return (size_t)n;
}

}  // namespace ag
