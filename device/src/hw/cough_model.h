// Cough classifier: int8 DS-CNN (ml/), run with TensorFlow Lite Micro.
#pragma once
#include <stdint.h>

namespace ag {
namespace cough {

bool init();                     // false if the model or arena is bad
// feat: 49x40 normalised log-mel (features.h). Returns P(cough) in [0,1].
float run(const float* feat);
// Raw int8 in/out (self-test against the TFLite reference vectors).
bool runQuantized(const int8_t* in, int8_t out[2]);
const char* status();            // "ok" or why init failed
uint32_t arenaUsed();

}  // namespace cough
}  // namespace ag
