// AsthmaGuard firmware entry point.
//
// Core 0: sensors (PPG 100 Hz, IMU 50 Hz), button, detector + state machine
//         (1 Hz), actuators, BLE, serial console.
// Core 1: audio capture + features + cough inference (sensors/audio.*).
//
// Serial console (115200): the same commands as the BLE `cmd`
// characteristic (TEST_ALERT, TEST_SERVO, ACK:<id>, SIM:..., TIME:<epoch>,
// CONFIG:{json}) plus STATUS, CONFIG?, EVENTS, VERBOSE:0|1, HELP.
// VERBOSE:1 prints every sensor once per second (phase 1 bring-up).
#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>

#include "actuators.h"
#include "ble.h"
#include "button.h"
#include "core.h"
#include "sensors/audio.h"
#include "sensors/cough_model.h"
#include "sensors/imu.h"
#include "sensors/ppg.h"

#ifndef AG_FIRMWARE_VERSION
#define AG_FIRMWARE_VERSION "dev"
#endif

using namespace ag;

namespace pins {
const uint8_t SDA = 4, SCL = 5;
const uint8_t I2S_BCLK = 10, I2S_DATA = 12;  // WS = 11 (BCLK + 1)
const uint8_t BUZZER = 13, VIB = 14, SERVO = 15, BUTTON = 16;
}  // namespace pins

static Core core;
static Imu imu(core.cfg);
static Ppg ppg;
static Actuators act(core.cfg);
static Button button(core.cfg);

static volatile bool g_core0_ready = false;
static bool g_verbose = false;
static bool g_dose_in_progress = false;
static uint32_t g_next_second = 0;
static uint32_t g_saved_next_id = 0;
static PpgResult g_ppg;

// ------------------------------------------------------------------ battery
// VSYS = LiPo through the TP4056 output. On the Pico 2 W the VSYS/3 divider
// sits on GPIO29, shared with the wireless chip; the Arduino-Pico core
// arbitrates it inside analogRead(A3) on W boards.
static float readVsys() {
  analogReadResolution(12);
  uint32_t acc = 0;
  for (int i = 0; i < 8; i++) acc += analogRead(A3);
  return (acc / 8.0f) * 3.0f * 3.3f / 4095.0f;
}

static uint8_t batteryPct(float v) {
  static const float V[] = {3.40f, 3.60f, 3.70f, 3.75f, 3.85f, 4.00f, 4.20f};
  static const float P[] = {0, 10, 20, 40, 60, 80, 100};
  if (v <= V[0]) return 0;
  for (int i = 1; i < 7; i++) {
    if (v <= V[i]) return (uint8_t)(P[i - 1] + (P[i] - P[i - 1]) * (v - V[i - 1]) / (V[i] - V[i - 1]));
  }
  return 100;
}

// ------------------------------------------------------------------ storage
static void loadEventId() {
  File f = LittleFS.open("/evid", "r");
  if (!f) return;
  uint32_t id = 0;
  if (f.read((uint8_t*)&id, 4) == 4 && id > 0) core.log.setNextId(id + 16);  // skip ids possibly sent before a crash
  f.close();
}

static void saveEventIdIfChanged() {
  uint32_t id = core.log.nextId();
  if (id == g_saved_next_id) return;
  File f = LittleFS.open("/evid", "w");
  if (!f) return;
  f.write((const uint8_t*)&id, 4);
  f.close();
  g_saved_next_id = id;
}

// ------------------------------------------------------------------ console
static void printStatus() {
  audio::Stats a = audio::stats();
  Serial.printf("fw %s state=%s alarm=%d episode=%d doses=%d dose_enabled=%d sim=%d pending=%u ble=%d\n",
                AG_FIRMWARE_VERSION, toString(core.sm.state()), (int)core.sm.alarm(), core.sm.episodeActive(),
                core.sm.dosesThisEpisode(), core.cfg.doseLimitsSet(), core.sim.enabled(),
                (unsigned)core.log.pending(), ble::connected());
  Serial.printf("ppg ok=%d imu ok=%d mic ok=%d model ok=%d time=%lu\n", ppg.ok(), imu.ok(), a.mic_ok, a.model_ok,
                (unsigned long)core.clock.epochAt(millis()));
}

static void printVerbose(uint32_t now) {
  audio::Stats a = audio::stats();
  Serial.printf(
      "[%6lus] hr=%5.1f spo2=%5.1f q=%d(%s) still=%d valid=%d ir=%lu red=%lu pi=%.4f R=%.2f | "
      "ax=%.2f ay=%.2f az=%.2f pitch=%6.1f act=%s std=%.3f | mic=%.1fdBFS inf=%lu/%lu %luus | c60=%d st=%s bat=%d\n",
      (unsigned long)(now / 1000), g_ppg.hr, g_ppg.spo2, g_ppg.quality_ok, g_ppg.why, imu.stillFor5s(),
      core.sig.ppg_valid, (unsigned long)ppg.lastIr(), (unsigned long)ppg.lastRed(), g_ppg.pi, g_ppg.r, imu.ax(),
      imu.ay(), imu.az(), imu.pitch(), toString(imu.activity()), imu.motionStd(), a.level_dbfs,
      (unsigned long)a.inferences, (unsigned long)a.silent_skips, (unsigned long)a.last_us, core.sig.cough_60s,
      toString(core.sm.state()), core.battery);
}

static void handleLine(char* line, uint32_t now) {
  if (!strcmp(line, "HELP")) {
    Serial.println("STATUS CONFIG? EVENTS VERBOSE:0|1 SELFTEST TEST_ALERT TEST_SERVO ACK:<id> TIME:<epoch> "
                   "CONFIG:{json} SIM:ON|OFF|clear|hr=|spo2=|valid=|cough=|act=|raise|btn=short|long|sos|bat=");
    return;
  }
  if (!strcmp(line, "STATUS")) { printStatus(); return; }
  if (!strcmp(line, "CONFIG?")) {
    static char buf[1536];
    if (core.cfg.toJson(buf, sizeof buf)) Serial.println(buf);
    return;
  }
  if (!strcmp(line, "EVENTS")) {
    char buf[220];
    for (size_t i = 0; i < core.log.pending(); i++) {
      if (EventLog::toJson(*core.log.at(i), buf, sizeof buf)) Serial.println(buf);
    }
    Serial.printf("%u pending, %lu dropped\n", (unsigned)core.log.pending(), (unsigned long)core.log.dropped());
    return;
  }
  if (!strncmp(line, "VERBOSE:", 8)) { g_verbose = line[8] == '1'; return; }
  if (!strcmp(line, "SELFTEST")) {
    audio::requestSelfTest();
    uint32_t t0 = millis();
    audio::SelfTest r;
    while (!(r = audio::selfTest()).done && millis() - t0 < 10000) delay(10);
    Serial.printf("model self-test: %d/%d exact, %d off by one LSB, %lu us/inference, model=%s arena=%lu\n",
                  r.exact, r.total, r.off_by_one, (unsigned long)r.us_per_inference, cough::status(),
                  (unsigned long)cough::arenaUsed());
    return;
  }
  char reply[128];
  core.command(line, now, reply, sizeof reply);
  Serial.println(reply);
}

static void pollSerial(uint32_t now) {
  static char line[1600];
  static size_t n = 0;
  while (Serial.available()) {
    int c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[n] = 0;
      if (n) handleLine(line, now);
      n = 0;
    } else if (n < sizeof(line) - 1) {
      line[n++] = (char)c;
    }
  }
}

// ------------------------------------------------------------------ core 0
void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) {}  // wait briefly for a USB console

  pinMode(pins::BUTTON, INPUT_PULLUP);
  act.begin(pins::BUZZER, pins::VIB, pins::SERVO);

  if (!LittleFS.begin()) Serial.println("LittleFS mount failed");
  if (!configLoad(core.cfg)) Serial.println("config: defaults");
  loadEventId();

  Wire.setSDA(pins::SDA);
  Wire.setSCL(pins::SCL);
  Wire.begin();
  Wire.setClock(400000);
  if (!imu.begin(Wire)) Serial.println("MPU6050 not found");
  if (!ppg.begin(Wire)) Serial.println("MAX30102 not found");
  audio::setSilenceGate(-75.0f);

  ble::begin(core, "AsthmaGuard");
  core.log.add(EventType::BOOT, 0, 0, "{\"fw\":\"" AG_FIRMWARE_VERSION "\"}");
  saveEventIdIfChanged();
  g_next_second = millis() + 1000;
  g_core0_ready = true;
  Serial.println("AsthmaGuard ready. Type HELP.");
}

void loop() {
  uint32_t now = millis();

  ppg.poll();
  if (imu.poll(now)) core.onArmRaise(now);

  ButtonEvent be = button.update(digitalRead(pins::BUTTON) == LOW, now);
  if (button.takeLongReached()) act.tick();
  if (be != ButtonEvent::NONE) core.onButton(be, now);

  audio::Score sc;
  while (audio::popScore(sc)) core.onCoughScore(sc.p, sc.t_ms);

  pollSerial(now);
  ble::poll(now);

  if ((int32_t)(now - g_next_second) >= 0) {
    g_next_second += 1000;
    if ((int32_t)(now - g_next_second) > 3000) g_next_second = now + 1000;  // fell far behind
    imu.endSecond();
    g_ppg = ppg.compute();
    SensorSample s;
    s.hr = g_ppg.hr;
    s.spo2 = g_ppg.spo2;
    // Invalid readings are never used: the signal must pass the quality
    // check AND the wrist must have been still for the last 5 s.
    s.ppg_valid = g_ppg.quality_ok && imu.stillFor5s();
    s.activity = imu.activity();
    s.battery = batteryPct(readVsys());
    core.secondTick(now, s);
    ble::notifyLive(now);
    if (g_verbose) printVerbose(now);
  }

  act.setAlarm(core.sm.alarm());
  if (core.sm.takePuffRequest()) {
    g_dose_in_progress = act.startPuff();
    if (!g_dose_in_progress) Serial.println("puff refused: servo busy");
  }
  if (core.takeTestServo()) act.testServo();
  if (core.takeTestAlert()) act.testAlert();
  if (act.update(now) && g_dose_in_progress) {
    g_dose_in_progress = false;
    core.onPuffDone(now);
  }
  if (core.takeConfigChanged()) {
    if (!configSave(core.cfg)) Serial.println("config save failed");
  }
  saveEventIdIfChanged();
}

// ------------------------------------------------------------------ core 1
void setup1() {
  while (!g_core0_ready) delay(1);
  if (!audio::begin(pins::I2S_BCLK, pins::I2S_DATA)) Serial.println("I2S mic start failed");
}

void loop1() { audio::loop(); }
