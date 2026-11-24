// BLE GATT server through BlueZ (D-Bus, sd-bus) on the Zero 2 W radio.
// Same service, UUIDs and JSON as the Pico build, so the app is unchanged.
//
// Service      a5f10000-7c1e-4b9a-9d3e-6a1f00000000
//   live       ...0001  read/notify (1 Hz)   {"hr":88,"spo2":97,"v":1,...}
//   event      ...0002  read/notify/indicate  pending events, oldest first
//   config     ...0003  read/write           thresholds, baselines, dose limits
//   time       ...0004  write                epoch seconds (ASCII)
//   cmd        ...0005  write/notify         TEST_ALERT, TEST_SERVO, ACK:<id>, SIM:<...>;
//                                             the reply ("OK ..."/"ERR ...") is notified back
//
// Events: every pending (un-ACKed) event is sent on `event`, one at a time,
// re-sent every 10 s until the phone writes ACK:<id> to `cmd`. An EMERGENCY
// or MANUAL_SOS jumps the queue.
//
// Everything runs on the main thread: D-Bus callbacks are dispatched from
// process(), which main.cpp calls in its loop.
#pragma once
#include <stdint.h>

#include "core.h"

namespace ag {
namespace ble {

// Registers the GATT application + advertisement with BlueZ (asynchronous;
// failures are reported through status()). Returns false if the system bus
// cannot be opened.
bool begin(Core& core, const char* name, const char* adapter = "/org/bluez/hci0");
int fd();                         // poll() this fd for input
int process(uint32_t now_ms);     // dispatch D-Bus messages + event delivery
void notifyLive(uint32_t now_ms); // once per second
bool connected();                 // a phone is connected
bool subscribed();                // the phone listens to `event`
const char* status();             // "advertising", "registering", or the error
void end();

}  // namespace ble
}  // namespace ag
