#include "sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ag {

static bool parseNum(const char* s, float lo, float hi, float& out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  double v = strtod(s, &end);
  if (end == s || *end != 0 || v < lo || v > hi) return false;
  out = (float)v;
  return true;
}

bool Sim::command(const char* cmd, SimActions& a) {
  if (!strncmp(cmd, "SIM:", 4)) cmd += 4;
  char key[16] = {0};
  const char* eq = strchr(cmd, '=');
  size_t kl = eq ? (size_t)(eq - cmd) : strlen(cmd);
  if (kl == 0 || kl >= sizeof(key)) return false;
  memcpy(key, cmd, kl);
  const char* val = eq ? eq + 1 : nullptr;
  float v;

  if (!strcmp(key, "ON")) { on_ = true; return true; }
  if (!strcmp(key, "OFF")) { *this = Sim(); return true; }
  if (!strcmp(key, "clear")) { bool o = on_; *this = Sim(); on_ = o; return true; }
  // Overrides switch simulation on, so "SIM:hr=125" alone is enough.
  if (!strcmp(key, "hr") && parseNum(val, 20, 250, v)) { hr_ = v; has_hr_ = on_ = true; return true; }
  if (!strcmp(key, "spo2") && parseNum(val, 50, 100, v)) { spo2_ = v; has_spo2_ = on_ = true; return true; }
  if (!strcmp(key, "valid") && parseNum(val, 0, 1, v)) { valid_ = v > 0.5f; has_valid_ = on_ = true; return true; }
  if (!strcmp(key, "cough") && parseNum(val, 0, 60, v)) { cough_ = (uint8_t)v; has_cough_ = on_ = true; return true; }
  if (!strcmp(key, "bat") && parseNum(val, 0, 100, v)) { bat_ = (uint8_t)v; has_bat_ = on_ = true; return true; }
  if (!strcmp(key, "act") && val) {
    Activity act;
    if (!parseActivity(val, act)) return false;
    act_ = act;
    has_act_ = on_ = true;
    return true;
  }
  if (!strcmp(key, "raise") && !val) { a.raise = true; return true; }
  if (!strcmp(key, "btn") && val) {
    if (!strcmp(val, "short")) a.button = ButtonEvent::SHORT;
    else if (!strcmp(val, "long")) a.button = ButtonEvent::LONG;
    else if (!strcmp(val, "sos")) a.button = ButtonEvent::SOS;
    else return false;
    return true;
  }
  return false;
}

void Sim::apply(Signals& s) const {
  if (!on_) return;
  if (has_hr_) s.hr = hr_;
  if (has_spo2_) s.spo2 = spo2_;
  if (has_hr_ || has_spo2_) s.ppg_valid = true;
  if (has_valid_) s.ppg_valid = valid_;
  if (has_cough_) s.cough_60s = cough_;
  if (has_act_) s.activity = act_;
}

bool Sim::battery(uint8_t& pct) const {
  if (!on_ || !has_bat_) return false;
  pct = bat_;
  return true;
}

void Sim::describe(char* buf, unsigned len) const {
  snprintf(buf, len, "sim=%s hr=%s%d spo2=%s%d valid=%s%d cough=%s%d act=%s%s",
           on_ ? "on" : "off", has_hr_ ? "" : "~", (int)hr_, has_spo2_ ? "" : "~", (int)spo2_,
           has_valid_ ? "" : "~", valid_, has_cough_ ? "" : "~", cough_, has_act_ ? "" : "~",
           toString(act_));
}

}  // namespace ag
