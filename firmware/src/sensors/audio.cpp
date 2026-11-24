#include "audio.h"

#include <Arduino.h>
#include <I2S.h>
#include <pico/mutex.h>
#include <pico/util/queue.h>

#include "cough_model.h"
#include "features.h"
#include "golden.h"

namespace ag {
namespace audio {

namespace {
using namespace ag::feat;

I2S g_i2s(INPUT);
queue_t g_scores;
auto_init_mutex(g_stats_mtx);  // usable from core 0 before begin()
Stats g_stats;
volatile float g_gate_dbfs = -75.0f;
volatile bool g_ready = false;  // queue initialised
volatile bool g_selftest_req = false;
SelfTest g_selftest;

int16_t g_ring[kWindow];
uint32_t g_wr = 0;            // total samples written
uint32_t g_next_infer = kWindow;
int16_t g_win[kWindow];
float g_feat[kFrames * kMel];
LogMel* g_lm = nullptr;

const uint32_t kStepSamples = kSampleRate / 4;  // 250 ms
// INMP441: 24-bit sample, MSB-aligned in a 32-bit slot. >>14 keeps the top
// 18 bits and gives ~12 dB of digital gain over >>16 for quiet coughs.
const int kShift = 14;

inline int16_t toPcm16(int32_t v) {
  int32_t s = v >> kShift;
  if (s > 32767) s = 32767;
  if (s < -32768) s = -32768;
  return (int16_t)s;
}
}  // namespace

bool begin(uint8_t pin_bclk, uint8_t pin_data) {
  queue_init(&g_scores, sizeof(Score), 16);
  static LogMel lm;  // ~19 KB, built once
  g_lm = &lm;
  g_i2s.setBCLK(pin_bclk);
  g_i2s.setDATA(pin_data);
  g_i2s.setBitsPerSample(32);
  g_i2s.setFrequency(kSampleRate);
  // Room for ~380 ms of audio while a window is being classified.
  g_i2s.setBuffers(24, 512);
  bool mic = g_i2s.begin();
  bool model = cough::init();
  mutex_enter_blocking(&g_stats_mtx);
  g_stats.mic_ok = mic;
  g_stats.model_ok = model;
  mutex_exit(&g_stats_mtx);
  g_ready = true;
  return mic;
}

void setSilenceGate(float dbfs) { g_gate_dbfs = dbfs; }

static void runSelfTest() {
  SelfTest r;
#if defined(AG_HAVE_GOLDEN)
  uint32_t t0 = micros();
  for (int i = 0; i < kGoldenN; i++) {
    int8_t out[2];
    if (!cough::runQuantized(kGoldenIn[i], out)) break;
    r.total++;
    int d0 = abs(out[0] - kGoldenOut[i][0]), d1 = abs(out[1] - kGoldenOut[i][1]);
    if (d0 == 0 && d1 == 0) r.exact++;
    else if (d0 <= 1 && d1 <= 1) r.off_by_one++;
  }
  r.us_per_inference = r.total ? (micros() - t0) / r.total : 0;
#endif
  r.done = true;
  mutex_enter_blocking(&g_stats_mtx);
  g_selftest = r;
  mutex_exit(&g_stats_mtx);
}

void requestSelfTest() {
  mutex_enter_blocking(&g_stats_mtx);
  g_selftest = SelfTest();
  mutex_exit(&g_stats_mtx);
  g_selftest_req = true;
}

SelfTest selfTest() {
  mutex_enter_blocking(&g_stats_mtx);
  SelfTest c = g_selftest;
  mutex_exit(&g_stats_mtx);
  return c;
}

void loop() {
  if (!g_lm) return;
  if (g_selftest_req) {
    g_selftest_req = false;
    runSelfTest();
  }
  int32_t l = 0, r = 0;
  // Read what is buffered (blocks for at most one frame when empty).
  for (int i = 0; i < 256; i++) {
    if (!g_i2s.read32(&l, &r)) break;
    g_ring[g_wr % kWindow] = toPcm16(l);  // L/R pin to GND -> left slot
    g_wr++;
  }
  if (g_wr < g_next_infer) return;
  g_next_infer = g_wr + kStepSamples;

  uint32_t t0 = micros();
  uint32_t start = g_wr - kWindow;
  for (uint32_t i = 0; i < (uint32_t)kWindow; i++) g_win[i] = g_ring[(start + i) % kWindow];
  float level = rmsDbfs(g_win, kWindow);
  Score s{0.0f, millis()};
  bool silent = level < g_gate_dbfs;
  bool model_ok;
  mutex_enter_blocking(&g_stats_mtx);
  model_ok = g_stats.model_ok;
  mutex_exit(&g_stats_mtx);
  if (!silent && model_ok) {
    computeWindow(*g_lm, g_win, g_feat);
    s.p = cough::run(g_feat);
  }
  uint32_t dt = micros() - t0;
  bool queued = queue_try_add(&g_scores, &s);

  mutex_enter_blocking(&g_stats_mtx);
  g_stats.level_dbfs = level;
  if (silent) g_stats.silent_skips++;
  else g_stats.inferences++;
  g_stats.last_us = dt;
  if (dt > g_stats.max_us) g_stats.max_us = dt;
  if (!queued) g_stats.dropped++;
  mutex_exit(&g_stats_mtx);
}

bool popScore(Score& s) { return g_ready && queue_try_remove(&g_scores, &s); }

Stats stats() {
  mutex_enter_blocking(&g_stats_mtx);
  Stats c = g_stats;
  mutex_exit(&g_stats_mtx);
  return c;
}

}  // namespace audio
}  // namespace ag
