// AsthmaGuard: Arduino-Pico timer (the upstream Arduino file needs the
// Nano 33 BLE "peripherals" code).
#include "tensorflow/lite/micro/micro_time.h"
#include <Arduino.h>
namespace tflite {
uint32_t ticks_per_second() { return 1000000; }
uint32_t GetCurrentTimeTicks() { return micros(); }
}  // namespace tflite
