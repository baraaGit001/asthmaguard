// Heart rate + SpO2 from raw MAX30102 red/IR samples (pure logic).
//
// 100 Hz samples, 4 s window, result every 1 s (section 5.2).
//  - HR: band-passed IR, beat peaks, median beat interval.
//  - SpO2: ratio of ratios R = (ACred/DCred) / (ACir/DCir), Maxim's curve
//          SpO2 = -45.060 R^2 + 30.354 R + 94.845.
//  - Quality: skin contact (IR DC level), perfusion index, beat regularity,
//    plausible ranges. The final `ppg_valid` also needs the IMU "still for
//    5 s" flag, which the caller adds.
#pragma once
#include <stdint.h>

namespace ag {

struct PpgResult {
  float hr = 0;
  float spo2 = 0;
  bool quality_ok = false;
  // Diagnostics (shown on the serial bring-up screen)
  float ir_dc = 0, pi = 0, r = 0, ibi_cv = 0;
  uint8_t beats = 0;
  const char* why = "warming up";  // reason when quality_ok is false
};

class PpgAlgo {
 public:
  static const int kRate = 100;
  static const int kWindow = 4 * kRate;

  void add(uint32_t red, uint32_t ir);  // one sample at 100 Hz
  bool ready() const { return n_ >= kWindow; }
  PpgResult compute() const;            // on the last 4 s
  void reset() { n_ = 0; head_ = 0; }

  // Tunables
  float min_ir_dc = 30000;   // below this: no skin contact
  float min_pi = 0.0005f;    // perfusion index (ACir/DCir) range
  float max_pi = 0.08f;
  float max_ibi_cv = 0.18f;  // beat interval coefficient of variation

 private:
  float red_[kWindow] = {}, ir_[kWindow] = {};
  int head_ = 0, n_ = 0;
};

}  // namespace ag
