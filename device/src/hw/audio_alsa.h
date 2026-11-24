// INMP441 on I2S through ALSA (config.txt: dtoverlay=googlevoicehat-soundcard).
// A capture thread keeps the last 1 s, and every 250 ms computes log-mel
// features and runs the cough model; scores go to the main loop through a
// mutex-protected queue.
#pragma once
#include <stdint.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace ag {
namespace audio {

struct Score {
  float p;        // P(cough), 0 when the window was silent
  uint32_t t_ms;  // monotonic ms at the end of the window
};

struct Stats {
  float level_dbfs = -120;
  uint32_t inferences = 0, silent_skips = 0, overruns = 0, dropped = 0;
  uint32_t last_us = 0, max_us = 0;
  bool mic_ok = false, model_ok = false;
  std::string error;
};

struct SelfTest {
  bool done = false;
  int total = 0, exact = 0, off_by_one = 0;
  uint32_t us_per_inference = 0;
};

// device: ALSA PCM name, e.g. "plughw:CARD=sndrpigooglevoi,DEV=0".
bool start(const std::string& device, float silence_gate_dbfs = -75.0f);
void stop();
bool popScore(Score& s);
Stats stats();
void requestSelfTest();
SelfTest selfTest();

uint32_t monoMs();  // the clock used for Score::t_ms (CLOCK_MONOTONIC)

}  // namespace audio
}  // namespace ag
