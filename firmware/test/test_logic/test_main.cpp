// Host tests: pio test -e native
// Every scenario of section 8 runs through simulation mode on the real
// detector + state machine (Core), plus unit tests of the pieces.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "button.h"
#include "core.h"
#include "sensors/motion.h"
#include "sensors/ppg_algo.h"

using namespace ag;

// ---------------------------------------------------------------- harness
struct H {
  Core c;
  uint32_t now = 1000;
  SensorSample raw;  // "real sensors": calm adult at rest
  H() {
    raw.hr = 72; raw.spo2 = 97; raw.ppg_valid = true; raw.activity = Activity::REST; raw.battery = 90;
    cmd("TIME:1764000000");
  }
  void cmd(const char* s) {
    char r[96];
    TEST_ASSERT_TRUE_MESSAGE(c.command(s, now, r, sizeof r), s);
  }
  // Advance n seconds; return true if `st` was ever seen.
  bool run(int n, State watch = State::NORMAL, bool* seen = nullptr) {
    for (int i = 0; i < n; i++) {
      now += 1000;
      c.secondTick(now, raw);
      if (c.sm.takePuffRequest()) { puffs++; c.onPuffDone(now); }
      if (seen && c.sm.state() == watch) *seen = true;
    }
    return seen && *seen;
  }
  State st() const { return c.sm.state(); }
  int count(EventType t) const {
    int n = 0;
    for (size_t i = 0; i < c.log.pending(); i++) if (c.log.at(i)->type == t) n++;
    return n;
  }
  const Event* last(EventType t) const {
    const Event* e = nullptr;
    for (size_t i = 0; i < c.log.pending(); i++) if (c.log.at(i)->type == t) e = c.log.at(i);
    return e;
  }
  void setDoseLimits(int max, int interval_s) {
    char b[64];
    snprintf(b, sizeof b, "CONFIG:{\"max_doses\":%d,\"dose_interval_s\":%d}", max, interval_s);
    cmd(b);
  }
  void attack() { cmd("SIM:cough=6"); cmd("SIM:hr=125"); cmd("SIM:act=REST"); }
  void calm() { cmd("SIM:cough=0"); cmd("SIM:hr=78"); cmd("SIM:spo2=97"); }
  void toAlert() { attack(); run(30); TEST_ASSERT_EQUAL((int)State::ALERT, (int)st()); }
  int puffs = 0;
};

#define ST(s) TEST_ASSERT_EQUAL_STRING(toString(s), toString(h.st()))

// ---------------------------------------------------------------- scenarios
void s01_normal_rest() {
  H h;
  bool bad = false;
  for (int i = 0; i < 600; i++) { h.run(1); if (h.st() != State::NORMAL) bad = true; }
  TEST_ASSERT_FALSE(bad);
  TEST_ASSERT_EQUAL(0, (int)h.c.log.pending());
}

void s02_jogging_no_alert() {
  H h;
  h.cmd("SIM:hr=150"); h.cmd("SIM:act=EXERCISE"); h.cmd("SIM:cough=0");
  bool alerted = false;
  for (int i = 0; i < 600; i++) {
    h.run(1);
    if (h.c.sm.alarm() != Alarm::NONE) alerted = true;
  }
  TEST_ASSERT_FALSE(alerted);
  TEST_ASSERT_EQUAL(0, h.count(EventType::ATTACK_START));
  // Even with coughs, EXERCISE blocks ATTACK_CONDITION.
  h.cmd("SIM:cough=6");
  h.run(120);
  TEST_ASSERT_EQUAL(0, h.count(EventType::ATTACK_START));
}

void s03_cough_only_suspect() {
  H h;
  h.cmd("SIM:cough=6");
  h.run(2);
  ST(State::SUSPECT);
  TEST_ASSERT_EQUAL(1, h.count(EventType::SUSPECT));
  h.run(300);
  ST(State::SUSPECT);
  TEST_ASSERT_TRUE(h.c.sm.alarm() == Alarm::NONE);
  // No sign for 60 s -> NORMAL
  h.cmd("SIM:cough=0");
  h.run(59);
  ST(State::SUSPECT);
  h.run(1);
  ST(State::NORMAL);
}

void s04_cough_hr_30s_alert() {
  H h;
  h.attack();
  h.run(29);
  ST(State::SUSPECT);
  h.run(1);
  ST(State::ALERT);
  TEST_ASSERT_EQUAL(1, h.count(EventType::ATTACK_START));
  TEST_ASSERT_TRUE(h.c.sm.alarm() == Alarm::ALERT);
  // Condition broken for one second resets the 30 s count.
  H g;
  g.attack();
  g.run(20);
  g.cmd("SIM:hr=80");
  g.run(1);
  g.cmd("SIM:hr=125");
  g.run(29);
  TEST_ASSERT_EQUAL_STRING("SUSPECT", toString(g.st()));
  g.run(1);
  TEST_ASSERT_EQUAL_STRING("ALERT", toString(g.st()));
}

void s05_false_alarm() {
  H h;
  h.toAlert();
  h.run(5);
  h.cmd("SIM:btn=long");
  ST(State::NORMAL);
  TEST_ASSERT_EQUAL(1, h.count(EventType::FALSE_ALARM));
  TEST_ASSERT_FALSE(h.c.sm.episodeActive());
  // After the 15 s window a long-press no longer cancels.
  H g;
  g.toAlert();
  g.run(15);
  TEST_ASSERT_EQUAL_STRING("WAIT_RAISE", toString(g.st()));
  g.cmd("SIM:btn=long");
  TEST_ASSERT_EQUAL_STRING("WAIT_RAISE", toString(g.st()));
  TEST_ASSERT_EQUAL(0, g.count(EventType::FALSE_ALARM));
}

void s06_raise_confirm_one_puff() {
  H h;
  h.setDoseLimits(2, 4 * 3600);
  h.toAlert();
  h.run(15);
  ST(State::WAIT_RAISE);
  TEST_ASSERT_TRUE(h.c.sm.alarm() == Alarm::RAISE);
  h.cmd("SIM:raise");
  ST(State::WAIT_CONFIRM);
  TEST_ASSERT_EQUAL(1, h.count(EventType::ARM_RAISED));
  h.cmd("SIM:btn=short");
  ST(State::DOSE);
  h.run(1);
  TEST_ASSERT_EQUAL(1, h.puffs);
  ST(State::MONITOR);
  TEST_ASSERT_EQUAL(1, h.count(EventType::DOSE));
  TEST_ASSERT_EQUAL(1, h.c.sm.dosesThisEpisode());
  // Improves for 3 min -> RESOLVED -> NORMAL
  h.calm();
  h.run(179);
  ST(State::MONITOR);
  TEST_ASSERT_EQUAL(1, h.count(EventType::IMPROVED));
  h.run(1);
  ST(State::RESOLVED);
  TEST_ASSERT_EQUAL(1, h.count(EventType::RESOLVED));
  h.run(1);
  ST(State::NORMAL);
  TEST_ASSERT_EQUAL(1, h.puffs);
}

void s07_raise_no_button_no_puff() {
  H h;
  h.setDoseLimits(2, 3600);
  h.toAlert();
  h.run(15);
  h.cmd("SIM:raise");
  ST(State::WAIT_CONFIRM);
  h.run(9);
  ST(State::WAIT_CONFIRM);
  h.run(1);
  ST(State::WAIT_RAISE);
  TEST_ASSERT_EQUAL(0, h.puffs);
  TEST_ASSERT_EQUAL(0, h.count(EventType::DOSE));
  // Arm raise alone, in any state, never doses.
  h.cmd("SIM:raise");
  h.run(30);
  TEST_ASSERT_EQUAL(0, h.puffs);
}

void s08_no_response_emergency() {
  H h;
  h.toAlert();
  h.run(15);
  ST(State::WAIT_RAISE);
  h.run(59);
  ST(State::WAIT_RAISE);
  h.run(1);
  ST(State::EMERGENCY);
  const Event* e = h.last(EventType::EMERGENCY);
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_NOT_NULL(strstr(e->data, "no_response"));
  TEST_ASSERT_TRUE(h.c.sm.alarm() == Alarm::EMERGENCY);
  // Phone not connected: stays in EMERGENCY, event stays pending.
  h.run(120);
  ST(State::EMERGENCY);
  TEST_ASSERT_TRUE(h.c.log.isPending(e->id));
  // Phone sends SMS and ACKs -> MONITOR, alarm continues.
  char ack[24];
  snprintf(ack, sizeof ack, "ACK:%lu", (unsigned long)h.c.sm.emergencyEventId());
  h.cmd(ack);
  ST(State::MONITOR);
  TEST_ASSERT_TRUE(h.c.sm.alarm() == Alarm::EMERGENCY);
}

void s09_spo2_critical() {
  H h;
  h.cmd("SIM:spo2=91");
  h.run(19);
  TEST_ASSERT_EQUAL(0, h.count(EventType::EMERGENCY));
  h.run(1);
  ST(State::ALERT);
  TEST_ASSERT_EQUAL(1, h.count(EventType::ATTACK_START));
  TEST_ASSERT_EQUAL(1, h.count(EventType::EMERGENCY));
  // Not re-sent every second while still critical.
  h.run(60);
  TEST_ASSERT_EQUAL(1, h.count(EventType::EMERGENCY));
  // A long-press cannot cancel once help has been called.
  H g;
  g.cmd("SIM:spo2=91");
  g.run(20);
  g.cmd("SIM:btn=long");
  TEST_ASSERT_EQUAL_STRING("ALERT", toString(g.st()));
  // Invalid readings (moving wrist) are never used.
  H k;
  k.cmd("SIM:spo2=91");
  k.cmd("SIM:valid=0");
  k.run(120);
  TEST_ASSERT_EQUAL_STRING("NORMAL", toString(k.st()));
}

void s10_dose_limit_then_emergency() {
  H h;
  h.setDoseLimits(1, 3600);
  h.toAlert();
  h.run(15);
  h.cmd("SIM:raise");
  h.cmd("SIM:btn=short");
  h.run(1);
  TEST_ASSERT_EQUAL(1, h.puffs);
  ST(State::MONITOR);
  // Not improved for 10 min, no dose left -> EMERGENCY
  h.run(599);
  ST(State::MONITOR);
  h.run(1);
  ST(State::EMERGENCY);
  TEST_ASSERT_NOT_NULL(strstr(h.last(EventType::EMERGENCY)->data, "not_improved"));
  TEST_ASSERT_EQUAL(1, h.puffs);

  // With doses left the device offers another puff, but the interval holds.
  H g;
  g.setDoseLimits(3, 3600);
  g.toAlert();
  g.run(15);
  g.cmd("SIM:raise");
  g.cmd("SIM:btn=short");
  g.run(601);
  // interval (1 h) not passed -> not allowed -> EMERGENCY
  TEST_ASSERT_EQUAL_STRING("EMERGENCY", toString(g.st()));
  TEST_ASSERT_EQUAL(1, g.puffs);

  H k;
  k.setDoseLimits(3, 300);  // 5 min interval -> second puff allowed after 10 min
  k.toAlert();
  k.run(15);
  k.cmd("SIM:raise");
  k.cmd("SIM:btn=short");
  k.run(601);
  TEST_ASSERT_EQUAL_STRING("ALERT", toString(k.st()));
  k.run(15);
  k.cmd("SIM:raise");
  k.cmd("SIM:btn=short");
  k.run(1);
  TEST_ASSERT_EQUAL(2, k.puffs);
  TEST_ASSERT_EQUAL(2, k.c.sm.dosesThisEpisode());
}

void s11_events_survive_disconnect() {
  H h;
  h.setDoseLimits(1, 3600);
  h.toAlert();
  h.run(15);
  h.cmd("SIM:raise");
  h.cmd("SIM:btn=short");
  h.run(1);
  // Nothing ACKed: all events still pending, in order, with unique ids.
  size_t n = h.c.log.pending();
  TEST_ASSERT_TRUE(n >= 4);
  for (size_t i = 1; i < n; i++) TEST_ASSERT_TRUE(h.c.log.at(i)->id > h.c.log.at(i - 1)->id);
  // Phone reconnects and ACKs one by one.
  while (h.c.log.pending()) {
    char ack[24];
    snprintf(ack, sizeof ack, "ACK:%lu", (unsigned long)h.c.log.at(0)->id);
    h.cmd(ack);
  }
  TEST_ASSERT_EQUAL(0, (int)h.c.log.pending());
  // Event JSON is well formed.
  EventLog log;
  uint32_t id = log.add(EventType::DOSE, 0, 50, "{\"n\":1}");
  log.backfillTime(1764000100, 60);
  char buf[200];
  TEST_ASSERT_TRUE(EventLog::toJson(*log.find(id), buf, sizeof buf) > 0);
  TEST_ASSERT_EQUAL_STRING("{\"id\":1,\"t\":1764000090,\"up\":50,\"type\":\"DOSE\",\"data\":{\"n\":1}}", buf);
}

void s12_no_dose_limits_servo_never_moves() {
  H h;  // doctor has not set limits
  TEST_ASSERT_FALSE(h.c.cfg.doseLimitsSet());
  h.toAlert();
  TEST_ASSERT_TRUE(h.c.sm.alarm() == Alarm::ALERT);  // alerts still work
  h.run(15);
  h.cmd("SIM:raise");
  h.cmd("SIM:btn=short");
  h.run(1);
  TEST_ASSERT_EQUAL(0, h.puffs);
  TEST_ASSERT_EQUAL(0, h.count(EventType::DOSE));
  TEST_ASSERT_NOT_NULL(strstr(h.last(EventType::DOSE_BLOCKED)->data, "limits_not_set"));
  ST(State::MONITOR);
  h.run(600);
  ST(State::EMERGENCY);
  TEST_ASSERT_EQUAL(0, h.puffs);
}

void s13_manual_sos_anytime() {
  H h;
  h.cmd("SIM:btn=sos");
  ST(State::EMERGENCY);
  TEST_ASSERT_EQUAL(1, h.count(EventType::MANUAL_SOS));
  TEST_ASSERT_TRUE(h.c.sm.episodeActive());
  // also from MONITOR and WAIT_CONFIRM
  H g;
  g.setDoseLimits(1, 60);
  g.toAlert();
  g.run(15);
  g.cmd("SIM:raise");
  g.cmd("SIM:btn=sos");
  TEST_ASSERT_EQUAL_STRING("EMERGENCY", toString(g.st()));
  TEST_ASSERT_EQUAL(0, g.puffs);
  // cancel with a long-press (e.g. pressed by mistake)
  g.cmd("SIM:btn=long");
  TEST_ASSERT_EQUAL_STRING("MONITOR", toString(g.st()));
  TEST_ASSERT_EQUAL(1, g.count(EventType::EMERGENCY_CANCELLED));
}

// ---------------------------------------------------------------- units
void u_button_gestures() {
  Config cfg;
  Button b(cfg);
  uint32_t t = 0;
  auto hold = [&](uint32_t ms) {
    ButtonEvent got = ButtonEvent::NONE;
    for (uint32_t i = 0; i < ms; i += 10) { t += 10; ButtonEvent e = b.update(true, t); if (e != ButtonEvent::NONE) got = e; }
    for (int i = 0; i < 10; i++) { t += 10; ButtonEvent e = b.update(false, t); if (e != ButtonEvent::NONE) got = e; }
    return got;
  };
  TEST_ASSERT_TRUE(hold(300) == ButtonEvent::SHORT);
  TEST_ASSERT_TRUE(hold(2500) == ButtonEvent::LONG);
  TEST_ASSERT_TRUE(hold(5200) == ButtonEvent::SOS);
  TEST_ASSERT_TRUE(hold(10) == ButtonEvent::NONE);  // bounce
}

void u_cough_counter() {
  Config cfg;
  CoughCounter cc(cfg);
  uint32_t t = 0;
  // One cough seen in 4 overlapping windows -> 1 event
  float scores[] = {0.1f, 0.9f, 0.95f, 0.9f, 0.85f, 0.2f};
  for (float s : scores) { cc.onScore(s, t); t += 250; }
  TEST_ASSERT_EQUAL(1, cc.count60(t));
  // Five separate coughs
  for (int i = 0; i < 5; i++) { cc.onScore(0.9f, t); t += 250; cc.onScore(0.1f, t); t += 2000; }
  TEST_ASSERT_EQUAL(6, cc.count60(t));
  // Below threshold ignored, window slides
  cc.onScore(0.79f, t);
  TEST_ASSERT_EQUAL(0, cc.count60(t + 61000));
}

void u_arm_raise() {
  Config cfg;
  ArmRaiseDetector d(cfg);
  uint32_t t = 0;
  int fired = 0;
  auto feed = [&](float p, int ms) { for (int i = 0; i < ms; i += 20) { t += 20; if (d.update(p, t)) fired++; } };
  feed(-70, 2000);                                     // arm hanging
  for (int i = 0; i <= 50; i++) { t += 20; if (d.update(-70 + i * 2.4f, t)) fired++; }  // +120 deg in 1 s
  feed(50, 300);
  TEST_ASSERT_EQUAL(0, fired);                         // not held long enough yet
  feed(50, 900);
  TEST_ASSERT_EQUAL(1, fired);
  feed(50, 3000);
  TEST_ASSERT_EQUAL(1, fired);                         // once per raise
  // Slow drift over 5 s is not a raise.
  ArmRaiseDetector d2(cfg);
  int f2 = 0;
  for (int i = 0; i < 250; i++) { t += 20; if (d2.update(-70 + i * 0.5f, t)) f2++; }
  for (int i = 0; i < 100; i++) { t += 20; if (d2.update(55, t)) f2++; }
  TEST_ASSERT_EQUAL(0, f2);
  // Quick wave down and back (not held) is not a raise.
  ArmRaiseDetector d3(cfg);
  int f3 = 0;
  for (int i = 0; i < 50; i++) { t += 20; if (d3.update(0, t)) f3++; }
  for (int i = 0; i < 20; i++) { t += 20; if (d3.update(80, t)) f3++; }
  for (int i = 0; i < 100; i++) { t += 20; if (d3.update(0, t)) f3++; }
  TEST_ASSERT_EQUAL(0, f3);
  // Lowering the arm after a raise is not a second raise.
  feed(-70, 3000);
  TEST_ASSERT_EQUAL(1, fired);
}

void u_activity() {
  ActivityTracker a;
  for (int s = 0; s < 60; s++) {
    for (int i = 0; i < 50; i++) a.addSample(0, 0, 1.0f + 0.005f * ((i % 2) ? 1 : -1));
    a.endSecond();
  }
  TEST_ASSERT_TRUE(a.activity() == Activity::REST);
  TEST_ASSERT_TRUE(a.stillFor5s());
  for (int s = 0; s < 60; s++) {
    for (int i = 0; i < 50; i++) a.addSample(0.3f * sinf(i * 0.6f), 0, 1.0f + 0.8f * sinf(i * 0.6f));
    a.endSecond();
  }
  TEST_ASSERT_TRUE(a.activity() == Activity::EXERCISE);
  TEST_ASSERT_FALSE(a.stillFor5s());
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 30.0f, pitchFromAccel(0.5f, 0, 0.866f));
}

void u_config_json() {
  Config c;
  char buf[1536];
  TEST_ASSERT_TRUE(c.toJson(buf, sizeof buf) > 0);
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"dose_enabled\":false"));
  Config d;
  d.hr_abs = 130;
  TEST_ASSERT_TRUE(d.fromJson(buf, strlen(buf), nullptr));
  TEST_ASSERT_EQUAL(110, d.hr_abs);
  const char* err = nullptr;
  const char* j1 = "{\"hr_abs\":20}";
  TEST_ASSERT_FALSE(d.fromJson(j1, strlen(j1), &err));
  TEST_ASSERT_EQUAL_STRING("value out of range", err);
  const char* j2 = "{\"hr_abs\":120,\"nope\":1}";
  TEST_ASSERT_FALSE(d.fromJson(j2, strlen(j2), &err));
  TEST_ASSERT_EQUAL_STRING("unknown key", err);
  TEST_ASSERT_EQUAL(110, d.hr_abs);  // nothing applied on error
  TEST_ASSERT_FALSE(d.fromJson("{bad", 4, &err));
  const char* j3 = "{\"max_doses\":2,\"dose_interval_s\":14400,\"baseline_hr\":68.5}";
  TEST_ASSERT_TRUE(d.fromJson(j3, strlen(j3), &err));
  TEST_ASSERT_TRUE(d.doseLimitsSet());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 68.5f, d.baseline_hr);
}

void u_live_json_and_commands() {
  H h;
  char buf[200];
  TEST_ASSERT_TRUE(h.c.liveJson(buf, sizeof buf) > 0);
  h.run(1);
  h.c.liveJson(buf, sizeof buf);
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"st\":\"NORMAL\""));
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"hr\":72"));
  char r[64];
  TEST_ASSERT_FALSE(h.c.command("SIM:hr=abc", h.now, r, sizeof r));
  TEST_ASSERT_FALSE(h.c.command("NOPE", h.now, r, sizeof r));
  TEST_ASSERT_FALSE(h.c.command("TIME:12", h.now, r, sizeof r));
  TEST_ASSERT_TRUE(h.c.command("TEST_ALERT", h.now, r, sizeof r));
  TEST_ASSERT_TRUE(h.c.takeTestAlert());
  h.cmd("SIM:bat=10");
  h.run(1);
  TEST_ASSERT_EQUAL(1, h.count(EventType::LOW_BATTERY));
  h.run(10);
  TEST_ASSERT_EQUAL(1, h.count(EventType::LOW_BATTERY));
  h.cmd("SIM:OFF");
  h.run(1);
  h.c.liveJson(buf, sizeof buf);
  TEST_ASSERT_NOT_NULL(strstr(buf, "\"sim\":0"));
}

void u_eventlog_overflow_keeps_critical() {
  EventLog log;
  log.add(EventType::EMERGENCY, 0, 0, nullptr);
  for (int i = 0; i < 100; i++) log.add(EventType::SUSPECT, 0, 0, nullptr);
  TEST_ASSERT_EQUAL(EventLog::kCap, (int)log.pending());
  TEST_ASSERT_TRUE(log.at(0)->type == EventType::EMERGENCY);
  TEST_ASSERT_TRUE(log.dropped() > 0);
}


static PpgResult synthPpg(float bpm, float R, float pi, float jitter, uint32_t dc_ir, unsigned seed) {
  PpgAlgo a;
  unsigned st = seed;
  auto rnd = [&]() { st = st * 1103515245u + 12345u; return ((st >> 8) & 0xFFFF) / 65535.0f - 0.5f; };
  float phase = 0;
  float f = bpm / 60.0f;
  for (int i = 0; i < 600; i++) {
    float t = i / 100.0f;
    float before = phase;
    phase += f / 100.0f;
    if (floorf(phase) != floorf(before)) f = bpm / 60.0f * (1 + jitter * rnd());  // new rate each beat
    float x = phase - floorf(phase);
    float shape = expf(-(x - 0.2f) * (x - 0.2f) / 0.01f);  // systolic bump
    float drift = 1 + 0.004f * sinf(2 * (float)M_PI * 0.25f * t);
    float ir = dc_ir * drift * (1 - pi * shape) + 20 * rnd();
    float red = 0.8f * dc_ir * drift * (1 - pi * R * shape) + 20 * rnd();
    a.add((uint32_t)red, (uint32_t)ir);
  }
  return a.compute();
}

void u_ppg_algo() {
  PpgResult r = synthPpg(72, 0.593f, 0.01f, 0.0f, 100000, 1);
  TEST_ASSERT_TRUE_MESSAGE(r.quality_ok, r.why);
  TEST_ASSERT_FLOAT_WITHIN(3.0f, 72.0f, r.hr);
  TEST_ASSERT_FLOAT_WITHIN(1.0f, 97.0f, r.spo2);
  r = synthPpg(125, 0.7827f, 0.006f, 0.02f, 80000, 2);
  TEST_ASSERT_TRUE_MESSAGE(r.quality_ok, r.why);
  TEST_ASSERT_FLOAT_WITHIN(4.0f, 125.0f, r.hr);
  TEST_ASSERT_FLOAT_WITHIN(1.5f, 91.0f, r.spo2);  // low perfusion + noise
  r = synthPpg(72, 0.593f, 0.01f, 0.0f, 5000, 3);       // strap loose
  TEST_ASSERT_FALSE(r.quality_ok);
  TEST_ASSERT_EQUAL_STRING("no skin contact", r.why);
  r = synthPpg(72, 0.593f, 0.2f, 0.0f, 100000, 4);      // huge swings = motion
  TEST_ASSERT_FALSE(r.quality_ok);
  r = synthPpg(70, 0.593f, 0.01f, 0.9f, 100000, 5);     // very irregular
  TEST_ASSERT_FALSE(r.quality_ok);
}

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(s01_normal_rest);
  RUN_TEST(s02_jogging_no_alert);
  RUN_TEST(s03_cough_only_suspect);
  RUN_TEST(s04_cough_hr_30s_alert);
  RUN_TEST(s05_false_alarm);
  RUN_TEST(s06_raise_confirm_one_puff);
  RUN_TEST(s07_raise_no_button_no_puff);
  RUN_TEST(s08_no_response_emergency);
  RUN_TEST(s09_spo2_critical);
  RUN_TEST(s10_dose_limit_then_emergency);
  RUN_TEST(s11_events_survive_disconnect);
  RUN_TEST(s12_no_dose_limits_servo_never_moves);
  RUN_TEST(s13_manual_sos_anytime);
  RUN_TEST(u_button_gestures);
  RUN_TEST(u_cough_counter);
  RUN_TEST(u_arm_raise);
  RUN_TEST(u_activity);
  RUN_TEST(u_config_json);
  RUN_TEST(u_live_json_and_commands);
  RUN_TEST(u_eventlog_overflow_keeps_critical);
  RUN_TEST(u_ppg_algo);
  return UNITY_END();
}
