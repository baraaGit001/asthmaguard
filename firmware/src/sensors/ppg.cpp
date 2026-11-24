#include "ppg.h"

#include <MAX30105.h>

namespace ag {

static MAX30105 g_max;

bool Ppg::begin(TwoWire& w) {
  dev_ = &g_max;
  if (!dev_->begin(w, I2C_SPEED_FAST)) return ok_ = false;
  // brightness 0x3F (~12.5 mA: the wrist needs more light than a finger),
  // no averaging, red + IR, 100 Hz, 411 us pulse (18 bit), 16384 nA range
  dev_->setup(0x3F, 1, 2, 100, 411, 16384);
  dev_->clearFIFO();
  return ok_ = true;
}

void Ppg::poll() {
  if (!ok_) return;
  dev_->check();
  while (dev_->available()) {
    last_red_ = dev_->getFIFORed();
    last_ir_ = dev_->getFIFOIR();
    algo_.add(last_red_, last_ir_);
    samples_++;
    dev_->nextSample();
  }
}

}  // namespace ag
