// BLE GATT service (section 5.3) on the Pico 2 W radio (BTstack).
//
// Service      a5f10000-7c1e-4b9a-9d3e-6a1f00000000
//   live       ...0001  notify (1 Hz)      {"hr":88,"spo2":97,"v":1,...}
//   event      ...0002  read/notify/indicate  pending events, oldest first
//   config     ...0003  read/write         thresholds, baselines, dose limits
//   time       ...0004  write              epoch seconds (ASCII)
//   cmd        ...0005  write/notify       TEST_ALERT, TEST_SERVO, ACK:<id>, SIM:<...>;
//                                           the reply ("OK ..."/"ERR ...") is notified back
//
// Events: every pending (un-ACKed) event is sent on `event`, one at a time,
// re-sent every 10 s until the phone writes ACK:<id> to `cmd`.
#pragma once
#include <stdint.h>

#include "core.h"

namespace ag {
namespace ble {

void begin(Core& core, const char* name);
void poll(uint32_t now_ms);      // every loop: event delivery, retries
void notifyLive(uint32_t now_ms);// once per second
bool connected();
uint32_t lastDeliveryFailMs();   // millis() of the last "EMERGENCY not delivered" (0 = none)

}  // namespace ble
}  // namespace ag
