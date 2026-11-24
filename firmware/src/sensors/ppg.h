// MAX30102 at 100 Hz (SparkFun MAX3010x driver) + PpgAlgo.
#pragma once
#include <Wire.h>

#include "ppg_algo.h"

class MAX30105;

namespace ag {

class Ppg {
 public:
  bool begin(TwoWire& w);
  void poll();               // drain the sensor FIFO, call every loop
  PpgResult compute() const { return algo_.compute(); }  // once per second
  bool ok() const { return ok_; }
  uint32_t lastIr() const { return last_ir_; }
  uint32_t lastRed() const { return last_red_; }
  uint32_t samples() const { return samples_; }
  PpgAlgo& algo() { return algo_; }

 private:
  MAX30105* dev_ = nullptr;
  PpgAlgo algo_;
  bool ok_ = false;
  uint32_t last_ir_ = 0, last_red_ = 0, samples_ = 0;
};

}  // namespace ag
