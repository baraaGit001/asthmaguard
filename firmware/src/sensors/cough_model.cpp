#include "cough_model.h"

#include <math.h>

#include "features.h"
#include "model_data.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace ag {
namespace cough {

namespace {
constexpr int kArenaSize = 64 * 1024;
alignas(16) uint8_t g_arena[kArenaSize];
alignas(16) uint8_t g_interp_buf[sizeof(tflite::MicroInterpreter)];
tflite::MicroInterpreter* g_interp = nullptr;
TfLiteTensor* g_in = nullptr;
TfLiteTensor* g_out = nullptr;
const char* g_status = "not started";
}  // namespace

bool init() {
  if (g_cough_model_len < 16) { g_status = "no model in firmware"; return false; }
  const tflite::Model* model = tflite::GetModel(g_cough_model);
  if (model->version() != TFLITE_SCHEMA_VERSION) { g_status = "model schema version"; return false; }
  static tflite::MicroMutableOpResolver<9> resolver;
  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddMean();
  resolver.AddAveragePool2D();
  resolver.AddFullyConnected();
  resolver.AddSoftmax();
  resolver.AddReshape();
  resolver.AddQuantize();
  resolver.AddDequantize();
  g_interp = new (g_interp_buf) tflite::MicroInterpreter(model, resolver, g_arena, kArenaSize);
  if (g_interp->AllocateTensors() != kTfLiteOk) { g_status = "arena too small"; g_interp = nullptr; return false; }
  g_in = g_interp->input(0);
  g_out = g_interp->output(0);
  if (g_in->type != kTfLiteInt8 || g_in->bytes != (size_t)(feat::kFrames * feat::kMel) || g_out->type != kTfLiteInt8) {
    g_status = "unexpected tensor shape/type";
    g_interp = nullptr;
    return false;
  }
  g_status = "ok";
  return true;
}

float run(const float* f) {
  if (!g_interp) return 0.0f;
  const float inv = 1.0f / kCoughInScale;
  int8_t* q = g_in->data.int8;
  for (int i = 0; i < feat::kFrames * feat::kMel; i++) {
    int v = (int)lroundf(f[i] * inv) + kCoughInZero;
    q[i] = (int8_t)(v < -128 ? -128 : (v > 127 ? 127 : v));
  }
  if (g_interp->Invoke() != kTfLiteOk) return 0.0f;
  return (g_out->data.int8[1] - kCoughOutZero) * kCoughOutScale;
}

bool runQuantized(const int8_t* in, int8_t out[2]) {
  if (!g_interp) return false;
  for (int i = 0; i < feat::kFrames * feat::kMel; i++) g_in->data.int8[i] = in[i];
  if (g_interp->Invoke() != kTfLiteOk) return false;
  out[0] = g_out->data.int8[0];
  out[1] = g_out->data.int8[1];
  return true;
}

const char* status() { return g_status; }
uint32_t arenaUsed() { return g_interp ? (uint32_t)g_interp->arena_used_bytes() : 0; }

}  // namespace cough
}  // namespace ag
