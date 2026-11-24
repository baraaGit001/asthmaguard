// Placeholder until ml/pipeline.py writes the trained model here.
#pragma once
#include <stdint.h>
extern const unsigned char g_cough_model[];
extern const unsigned int g_cough_model_len;
constexpr float kCoughInScale = 1.0f;
constexpr int kCoughInZero = 0;
constexpr float kCoughOutScale = 1.0f / 256;
constexpr int kCoughOutZero = -128;
