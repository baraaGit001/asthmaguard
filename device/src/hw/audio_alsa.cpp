#include "hw/audio_alsa.h"

#include <alsa/asoundlib.h>
#include <stdlib.h>
#include <time.h>

#include <vector>

#include "cough_model.h"
#include "golden.h"
#include "sensors/features.h"

namespace ag {
namespace audio {

namespace {
using namespace ag::feat;

std::thread g_thread;
std::atomic<bool> g_run{false};
std::atomic<bool> g_selftest_req{false};
std::mutex g_mtx;  // guards g_scores, g_stats, g_selftest
std::deque<Score> g_scores;
Stats g_stats;
SelfTest g_selftest;
float g_gate = -75.0f;
snd_pcm_t* g_pcm = nullptr;
unsigned g_channels = 2;

const uint32_t kStep = kSampleRate / 4;  // 250 ms
const size_t kMaxQueue = 32;
// INMP441: 24-bit sample, MSB-aligned in a 32-bit slot. >>14 keeps the top
// 18 bits = ~12 dB of digital gain over >>16 (same as the Pico build).
const int kShift = 14;

int16_t toPcm16(int32_t v) {
  int32_t s = v >> kShift;
  if (s > 32767) s = 32767;
  if (s < -32768) s = -32768;
  return (int16_t)s;
}

uint32_t nowUs() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000000ull + ts.tv_nsec / 1000);
}

bool openPcm(const std::string& dev, std::string& err) {
  int rc = snd_pcm_open(&g_pcm, dev.c_str(), SND_PCM_STREAM_CAPTURE, 0);
  if (rc < 0) { err = std::string("open ") + dev + ": " + snd_strerror(rc); return false; }
  snd_pcm_hw_params_t* hw;
  snd_pcm_hw_params_alloca(&hw);
  snd_pcm_hw_params_any(g_pcm, hw);
  snd_pcm_hw_params_set_access(g_pcm, hw, SND_PCM_ACCESS_RW_INTERLEAVED);
  if ((rc = snd_pcm_hw_params_set_format(g_pcm, hw, SND_PCM_FORMAT_S32_LE)) < 0) { err = "format S32_LE"; return false; }
  g_channels = 2;  // the I2S overlay is stereo; INMP441 L/R=GND -> left slot
  if (snd_pcm_hw_params_set_channels(g_pcm, hw, 2) < 0) {
    g_channels = 1;
    if (snd_pcm_hw_params_set_channels(g_pcm, hw, 1) < 0) { err = "channels"; return false; }
  }
  unsigned rate = kSampleRate;
  if ((rc = snd_pcm_hw_params_set_rate_near(g_pcm, hw, &rate, nullptr)) < 0 || rate != (unsigned)kSampleRate) {
    err = "rate 16000 not available (use a plughw: device)";
    return false;
  }
  snd_pcm_uframes_t period = 1000, buffer = 8000;  // 62.5 ms periods, 0.5 s buffer
  snd_pcm_hw_params_set_period_size_near(g_pcm, hw, &period, nullptr);
  snd_pcm_hw_params_set_buffer_size_near(g_pcm, hw, &buffer);
  if ((rc = snd_pcm_hw_params(g_pcm, hw)) < 0) { err = std::string("hw_params: ") + snd_strerror(rc); return false; }
  return snd_pcm_prepare(g_pcm) >= 0;
}

void runSelfTest() {
  SelfTest r;
#if defined(AG_HAVE_GOLDEN)
  uint32_t t0 = nowUs();
  for (int i = 0; i < kGoldenN; i++) {
    int8_t out[2];
    if (!cough::runQuantized(kGoldenIn[i], out)) break;
    r.total++;
    int d0 = abs(out[0] - kGoldenOut[i][0]), d1 = abs(out[1] - kGoldenOut[i][1]);
    if (d0 == 0 && d1 == 0) r.exact++;
    else if (d0 <= 1 && d1 <= 1) r.off_by_one++;
  }
  r.us_per_inference = r.total ? (nowUs() - t0) / r.total : 0;
#endif
  r.done = true;
  std::lock_guard<std::mutex> l(g_mtx);
  g_selftest = r;
}

void loop() {
  std::vector<int32_t> buf(1000 * 2);
  std::vector<int16_t> ring(kWindow), win(kWindow);
  std::vector<float> feat(kFrames * kMel);
  static LogMel lm;
  uint64_t written = 0, next_infer = kWindow;
  bool model_ok;
  {
    std::lock_guard<std::mutex> l(g_mtx);
    model_ok = g_stats.model_ok;
  }
  while (g_run) {
    if (g_selftest_req.exchange(false)) runSelfTest();
    snd_pcm_sframes_t n = snd_pcm_readi(g_pcm, buf.data(), 1000);
    if (n < 0) {
      if (n == -EPIPE) {
        std::lock_guard<std::mutex> l(g_mtx);
        g_stats.overruns++;
      }
      snd_pcm_recover(g_pcm, (int)n, 1);
      continue;
    }
    for (snd_pcm_sframes_t i = 0; i < n; i++) ring[(written + i) % kWindow] = toPcm16(buf[i * g_channels]);
    written += (uint64_t)n;
    if (written < next_infer) continue;
    next_infer = written + kStep;

    uint32_t t0 = nowUs();
    uint64_t start = written - kWindow;
    for (int i = 0; i < kWindow; i++) win[i] = ring[(start + i) % kWindow];
    float level = rmsDbfs(win.data(), kWindow);
    Score s{0.0f, monoMs()};
    bool silent = level < g_gate;
    if (!silent && model_ok) {
      computeWindow(lm, win.data(), feat.data());
      s.p = cough::run(feat.data());
    }
    uint32_t dt = nowUs() - t0;
    std::lock_guard<std::mutex> l(g_mtx);
    if (g_scores.size() >= kMaxQueue) {
      g_scores.pop_front();
      g_stats.dropped++;
    }
    g_scores.push_back(s);
    g_stats.level_dbfs = level;
    if (silent) g_stats.silent_skips++;
    else g_stats.inferences++;
    g_stats.last_us = dt;
    if (dt > g_stats.max_us) g_stats.max_us = dt;
  }
}
}  // namespace

uint32_t monoMs() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint32_t)(ts.tv_sec * 1000ull + ts.tv_nsec / 1000000);
}

bool start(const std::string& device, float gate) {
  g_gate = gate;
  std::string err;
  bool model = cough::init();
  bool mic = openPcm(device, err);
  {
    std::lock_guard<std::mutex> l(g_mtx);
    g_stats.model_ok = model;
    g_stats.mic_ok = mic;
    g_stats.error = mic ? (model ? "" : cough::status()) : err;
  }
  if (!mic) {
    if (g_pcm) snd_pcm_close(g_pcm);
    g_pcm = nullptr;
    return false;
  }
  g_run = true;
  g_thread = std::thread(loop);
  return true;
}

void stop() {
  g_run = false;
  if (g_thread.joinable()) g_thread.join();
  if (g_pcm) snd_pcm_close(g_pcm);
  g_pcm = nullptr;
}

bool popScore(Score& s) {
  std::lock_guard<std::mutex> l(g_mtx);
  if (g_scores.empty()) return false;
  s = g_scores.front();
  g_scores.pop_front();
  return true;
}

Stats stats() {
  std::lock_guard<std::mutex> l(g_mtx);
  return g_stats;
}

void requestSelfTest() {
  {
    std::lock_guard<std::mutex> l(g_mtx);
    g_selftest = SelfTest();
  }
  if (g_run) g_selftest_req = true;
  else runSelfTest();  // no mic thread: run here (model is only used by one thread at a time)
}

SelfTest selfTest() {
  std::lock_guard<std::mutex> l(g_mtx);
  return g_selftest;
}

}  // namespace audio
}  // namespace ag
