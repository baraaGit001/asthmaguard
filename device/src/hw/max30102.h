// MAX30102 register driver (I2C 0x57), 100 Hz red + IR into PpgAlgo.
// Replaces the SparkFun Arduino library used on the Pico.
#pragma once
#include <stdint.h>

#include "hw/i2c.h"
#include "sensors/ppg_algo.h"

namespace ag {

class Max30102 {
 public:
  bool begin(I2cBus& bus, uint8_t addr = 0x57);
  void poll();  // drain the FIFO; call every few ms (FIFO holds 32 samples = 320 ms)
  PpgResult compute() const { return algo_.compute(); }
  bool ok() const { return ok_; }
  uint8_t partId() const { return part_; }
  uint32_t lastIr() const { return last_ir_; }
  uint32_t lastRed() const { return last_red_; }
  uint32_t samples() const { return samples_; }
  uint32_t overflows() const { return overflows_; }

 private:
  I2cBus* bus_ = nullptr;
  uint8_t addr_ = 0x57, part_ = 0;
  bool ok_ = false;
  PpgAlgo algo_;
  uint32_t last_ir_ = 0, last_red_ = 0, samples_ = 0, overflows_ = 0;
};

}  // namespace ag
