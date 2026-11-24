// The cough model run through TensorFlow Lite Micro (as on the Zero 2 W)
// must give the same int8 outputs as the TFLite reference interpreter used
// in training (golden.h, written by ml/pipeline.py). Also times inference.
#include <chrono>
#include <stdio.h>
#include <stdlib.h>
#include <unity.h>

#include "cough_model.h"
#include "golden.h"
#include "model_data.h"

using namespace ag;

void setUp() {}
void tearDown() {}

void test_model_loads() {
  if (g_cough_model_len < 16) TEST_IGNORE_MESSAGE("placeholder model (no trained model in device/model yet)");
  TEST_ASSERT_TRUE_MESSAGE(cough::init(), cough::status());
  printf("arena used: %u bytes\n", (unsigned)cough::arenaUsed());
}

void test_matches_tflite_reference() {
#if defined(AG_HAVE_GOLDEN)
  if (!cough::init()) TEST_FAIL_MESSAGE(cough::status());
  int exact = 0, near = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < kGoldenN; i++) {
    int8_t out[2];
    TEST_ASSERT_TRUE(cough::runQuantized(kGoldenIn[i], out));
    int d0 = abs(out[0] - kGoldenOut[i][0]), d1 = abs(out[1] - kGoldenOut[i][1]);
    if (d0 == 0 && d1 == 0) exact++;
    else if (d0 <= 1 && d1 <= 1) near++;
    else {
      char m[96];
      snprintf(m, sizeof m, "vector %d: got (%d,%d) want (%d,%d)", i, out[0], out[1], kGoldenOut[i][0], kGoldenOut[i][1]);
      TEST_FAIL_MESSAGE(m);
    }
  }
  double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / kGoldenN;
  printf("golden: %d/%d exact, %d off by one LSB, %.0f us/inference\n", exact, kGoldenN, near, us);
#else
  TEST_IGNORE_MESSAGE("no golden vectors yet");
#endif
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_model_loads);
  RUN_TEST(test_matches_tflite_reference);
  return UNITY_END();
}
