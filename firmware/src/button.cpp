#include "button.h"

namespace ag {

ButtonEvent Button::update(bool pressed, uint32_t now_ms) {
  if (pressed != raw_) {
    raw_ = pressed;
    raw_since_ = now_ms;
  }
  if (raw_ != down_ && now_ms - raw_since_ >= kDebounceMs) {
    down_ = raw_;
    if (down_) {
      down_since_ = raw_since_;
      sos_fired_ = false;
      long_seen_ = false;
    } else {
      uint32_t held = raw_since_ - down_since_;
      if (sos_fired_) return ButtonEvent::NONE;
      if (held >= cfg_.long_press_ms) return ButtonEvent::LONG;
      return ButtonEvent::SHORT;
    }
  }
  if (down_) {
    uint32_t held = now_ms - down_since_;
    if (!long_seen_ && held >= cfg_.long_press_ms) {
      long_seen_ = true;
      long_tick_ = true;
    }
    if (!sos_fired_ && held >= cfg_.sos_press_ms) {
      sos_fired_ = true;
      return ButtonEvent::SOS;
    }
  }
  return ButtonEvent::NONE;
}

}  // namespace ag
