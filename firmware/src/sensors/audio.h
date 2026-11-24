// Core 1: I2S capture (INMP441, 16 kHz), log-mel features and cough
// inference every 250 ms on the last 1 s. Scores go to core 0 through a
// lock-free queue (pico/util/queue).
#pragma once
#include <stdint.h>

namespace ag {
namespace audio {

struct Score {
  float p;          // P(cough), 0 when the window was silent
  uint32_t t_ms;    // millis() at the end of the window
};

struct Stats {
  float level_dbfs = -120;   // RMS of the last 1 s window
  uint32_t inferences = 0;
  uint32_t silent_skips = 0;
  uint32_t last_us = 0;      // features + inference time of the last window
  uint32_t max_us = 0;
  uint32_t dropped = 0;      // scores lost because core 0 did not keep up
  bool mic_ok = false;
  bool model_ok = false;
};

// Pins: BCLK, DATA (WS is BCLK + 1 for the Arduino-Pico I2S class).
bool begin(uint8_t pin_bclk, uint8_t pin_data);  // call from setup1()
void loop();                                     // call from loop1()
bool popScore(Score& s);                         // core 0
Stats stats();                                   // core 0, a copy
void setSilenceGate(float dbfs);                 // windows below this are not classified

// Runs the golden vectors (lib/model/golden.h, made by ml/pipeline.py from
// the TFLite reference interpreter) through the on-device model on core 1.
struct SelfTest {
  bool done = false;
  int total = 0, exact = 0, off_by_one = 0;
  uint32_t us_per_inference = 0;
};
void requestSelfTest();
SelfTest selfTest();

}  // namespace audio
}  // namespace ag
