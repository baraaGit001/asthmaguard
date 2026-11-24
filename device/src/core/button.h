// Button gestures from raw level samples (pure logic, debounced).
//   released < long_press_ms           -> SHORT
//   released in [long_press_ms, sos)   -> LONG  (cancel / false alarm)
//   held for sos_press_ms              -> SOS   (fires while still held)
#pragma once
#include <stdint.h>

#include "config.h"
#include "types.h"

namespace ag {

class Button {
 public:
  explicit Button(const Config& cfg) : cfg_(cfg) {}
  // pressed = debounced-or-raw "button is down". Call often (every loop).
  ButtonEvent update(bool pressed, uint32_t now_ms);
  // True once when a hold passes long_press_ms (for a short haptic tick).
  bool takeLongReached() { bool r = long_tick_; long_tick_ = false; return r; }
  bool held() const { return down_; }

 private:
  static const uint32_t kDebounceMs = 30;
  const Config& cfg_;
  bool raw_ = false, down_ = false;
  uint32_t raw_since_ = 0, down_since_ = 0;
  bool sos_fired_ = false, long_tick_ = false, long_seen_ = false;
};

}  // namespace ag
