#include "config.h"

#include <ArduinoJson.h>
#include <stddef.h>
#include <string.h>

namespace ag {

namespace {

enum class T : uint8_t { U8, U16, U32, F32 };

struct Field {
  const char* key;
  T type;
  size_t off;
  float lo, hi;
};

#define F(name, type, lo, hi) {#name, T::type, offsetof(Config, name), lo, hi}
const Field kFields[] = {
    F(cough_score_min, F32, 0.5f, 0.99f),
    F(cough_gap_ms, U16, 100, 2000),
    F(cough_sign, U8, 1, 30),
    F(hr_rise, U8, 5, 60),
    F(hr_abs, U8, 80, 200),
    F(spo2_sign, U8, 85, 99),
    F(spo2_crit, U8, 80, 98),
    F(spo2_crit_s, U16, 5, 120),
    F(attack_hold_s, U16, 5, 300),
    F(suspect_clear_s, U16, 10, 600),
    F(cancel_window_s, U16, 5, 60),
    F(raise_timeout_s, U16, 15, 600),
    F(confirm_timeout_s, U16, 3, 60),
    F(improve_hold_s, U16, 30, 1800),
    F(monitor_timeout_s, U16, 60, 3600),
    F(reescalate_s, U16, 60, 3600),
    F(emergency_retry_s, U16, 2, 120),
    F(improve_spo2, U8, 85, 99),
    F(improve_hr_margin, U8, 0, 60),
    F(improve_cough_max, U8, 0, 10),
    F(baseline_hr, F32, 40, 120),
    F(baseline_spo2, F32, 85, 100),
    F(calibrated_at, U32, 0, 4.2e9f),
    F(max_doses, U8, 0, 8),
    F(dose_interval_s, U32, 0, 86400),
    F(servo_rest, U8, 0, 180),
    F(servo_press, U8, 0, 180),
    F(servo_hold_ms, U16, 100, 3000),
    F(raise_deg, U8, 20, 150),
    F(raise_within_ms, U16, 300, 5000),
    F(raise_hold_ms, U16, 200, 5000),
    F(raise_invert, U8, 0, 1),
    F(long_press_ms, U16, 500, 4000),
    F(sos_press_ms, U16, 3000, 15000),
    F(low_battery_pct, U8, 5, 50),
};
#undef F

float readField(const Config& c, const Field& f) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(&c) + f.off;
  switch (f.type) {
    case T::U8: return *p;
    case T::U16: { uint16_t v; memcpy(&v, p, 2); return v; }
    case T::U32: { uint32_t v; memcpy(&v, p, 4); return (float)v; }
    case T::F32: { float v; memcpy(&v, p, 4); return v; }
  }
  return 0;
}

void writeField(Config& c, const Field& f, double v) {
  uint8_t* p = reinterpret_cast<uint8_t*>(&c) + f.off;
  switch (f.type) {
    case T::U8: *p = (uint8_t)(v + 0.5); break;
    case T::U16: { uint16_t x = (uint16_t)(v + 0.5); memcpy(p, &x, 2); break; }
    case T::U32: { uint32_t x = (uint32_t)(v + 0.5); memcpy(p, &x, 4); break; }
    case T::F32: { float x = (float)v; memcpy(p, &x, 4); break; }
  }
}

}  // namespace

size_t Config::toJson(char* buf, size_t len) const {
  JsonDocument doc;
  for (const Field& f : kFields) {
    if (f.type == T::F32) doc[f.key] = readField(*this, f);
    else if (f.type == T::U32) {
      uint32_t v; memcpy(&v, reinterpret_cast<const uint8_t*>(this) + f.off, 4);
      doc[f.key] = v;
    } else doc[f.key] = (uint32_t)readField(*this, f);
  }
  doc["dose_enabled"] = doseLimitsSet();
  size_t need = measureJson(doc);
  if (need + 1 > len) return 0;
  return serializeJson(doc, buf, len);
}

bool Config::fromJson(const char* json, size_t len, const char** err) {
  static const char* kBadJson = "bad json";
  static const char* kNotObject = "not an object";
  static const char* kBadValue = "value out of range";
  static const char* kUnknown = "unknown key";
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) { if (err) *err = kBadJson; return false; }
  if (!doc.is<JsonObject>()) { if (err) *err = kNotObject; return false; }
  JsonObject obj = doc.as<JsonObject>();

  // Validate everything first, then apply, so a bad write changes nothing.
  for (JsonPair kv : obj) {
    const char* k = kv.key().c_str();
    if (strcmp(k, "dose_enabled") == 0) continue;  // derived, ignored
    const Field* found = nullptr;
    for (const Field& f : kFields) if (strcmp(f.key, k) == 0) { found = &f; break; }
    if (!found) { if (err) *err = kUnknown; return false; }
    if (!kv.value().is<double>()) { if (err) *err = kBadValue; return false; }
    double v = kv.value().as<double>();
    if (v < found->lo || v > found->hi) { if (err) *err = kBadValue; return false; }
  }
  Config next = *this;
  for (JsonPair kv : obj) {
    for (const Field& f : kFields) {
      if (strcmp(f.key, kv.key().c_str()) == 0) writeField(next, f, kv.value().as<double>());
    }
  }
  if (next.servo_press == next.servo_rest) { if (err) *err = kBadValue; return false; }
  *this = next;
  return true;
}

#ifdef ARDUINO
}  // namespace ag
#include <LittleFS.h>
namespace ag {

static const char* kConfigPath = "/config.json";

bool configLoad(Config& c) {
  File f = LittleFS.open(kConfigPath, "r");
  if (!f) return false;
  static char buf[1536];
  size_t n = f.readBytes(buf, sizeof(buf) - 1);
  f.close();
  buf[n] = 0;
  return c.fromJson(buf, n, nullptr);
}

bool configSave(const Config& c) {
  static char buf[1536];
  size_t n = c.toJson(buf, sizeof(buf));
  if (!n) return false;
  File f = LittleFS.open("/config.tmp", "w");
  if (!f) return false;
  bool ok = f.write((const uint8_t*)buf, n) == n;
  f.close();
  if (!ok) return false;
  LittleFS.remove(kConfigPath);
  return LittleFS.rename("/config.tmp", kConfigPath);
}
#else
bool configLoad(Config&) { return false; }
bool configSave(const Config&) { return true; }
#endif

}  // namespace ag
