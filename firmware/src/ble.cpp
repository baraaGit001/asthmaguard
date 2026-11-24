#include "ble.h"

#include <Arduino.h>
#include <BLE.h>
#include <string.h>

namespace ag {
namespace ble {

namespace {

#define AG_UUID(n) "a5f1000" n "-7c1e-4b9a-9d3e-6a1f00000000"
const char* kService = AG_UUID("0");
const char* kLive = AG_UUID("1");
const char* kEvent = AG_UUID("2");
const char* kConfig = AG_UUID("3");
const char* kTime = AG_UUID("4");
const char* kCmd = AG_UUID("5");

const uint32_t kResendMs = 10000;   // un-ACKed event is re-sent after this
const uint32_t kNextGapMs = 150;    // pacing between two different events

Core* g_core = nullptr;

// Writes arrive in the BTstack context; keep them in small mailboxes and
// handle them from loop() so the logic never runs on two contexts at once.
struct Mailbox {
  volatile bool full = false;
  char buf[256];
};
Mailbox g_cmd_box, g_cfg_box, g_time_box;

void stash(Mailbox& m, BLECharacteristic* c) {
  size_t n = c->valueLen();
  if (m.full) return;  // previous one not handled yet: drop (the app retries)
  if (n >= sizeof(m.buf)) n = sizeof(m.buf) - 1;
  memcpy(m.buf, c->valueData(), n);
  m.buf[n] = 0;
  m.full = true;
}

class Service : public BLEService, public BLECharacteristicCallbacks, public BLEServerCallbacks {
 public:
  Service() : BLEService(BLEUUID(kService)) {
    live = new BLECharacteristic(BLEUUID(kLive), BLERead | BLENotify, "live");
    event = new BLECharacteristic(BLEUUID(kEvent), BLERead | BLENotify | BLEIndicate, "event");
    config = new BLECharacteristic(BLEUUID(kConfig), BLERead | BLEWrite, "config");
    time = new BLECharacteristic(BLEUUID(kTime), BLEWrite, "time");
    cmd = new BLECharacteristic(BLEUUID(kCmd), BLEWrite | BLENotify, "cmd");
    for (BLECharacteristic* c : {live, event, config, time, cmd}) {
      c->setCallbacks(this);
      addCharacteristic(c);
    }
    live->setValue(String("{}"));
    event->setValue(String("{}"));
    cmd->setValue(String(""));
  }

  BLECharacteristic *live, *event, *config, *time, *cmd;
  volatile bool is_connected = false;
  volatile bool just_connected = false;

 private:
  void onWrite(BLECharacteristic* c) override {
    if (c == cmd) stash(g_cmd_box, c);
    else if (c == config) stash(g_cfg_box, c);
    else if (c == time) stash(g_time_box, c);
  }
  void onConnect(BLEServer*) override {
    is_connected = true;
    just_connected = true;
  }
  void onDisconnect(BLEServer*) override {
    is_connected = false;
    BLE.startAdvertising();
  }
};

Service* g_svc = nullptr;
uint32_t g_inflight_id = 0;
uint32_t g_inflight_ms = 0;
uint32_t g_last_send_ms = 0;
uint32_t g_fail_ms = 0;
bool g_fail_logged = false;
char g_json[1536];

void publishConfig() {
  size_t n = g_core->cfg.toJson(g_json, sizeof g_json);
  if (n) g_svc->config->setValue((const uint8_t*)g_json, n);
}

// Oldest pending event, but an EMERGENCY / MANUAL_SOS jumps the queue.
const Event* nextToSend() {
  const EventLog& log = g_core->log;
  for (size_t i = 0; i < log.pending(); i++) {
    const Event* e = log.at(i);
    if (e->type == EventType::EMERGENCY || e->type == EventType::MANUAL_SOS) return e;
  }
  return log.at(0);
}

void handleMailboxes(uint32_t now) {
  char reply[128];
  if (g_time_box.full) {
    char cmd[40];
    snprintf(cmd, sizeof cmd, "TIME:%s", g_time_box.buf);
    g_core->command(cmd, now, reply, sizeof reply);
    g_time_box.full = false;
    g_svc->cmd->setValue(String(reply));
  }
  if (g_cfg_box.full) {
    static char cmd[300];
    snprintf(cmd, sizeof cmd, "CONFIG:%s", g_cfg_box.buf);
    g_core->command(cmd, now, reply, sizeof reply);
    g_cfg_box.full = false;
    publishConfig();
    g_svc->cmd->setValue(String(reply));  // "OK config" or "ERR value out of range"
  }
  if (g_cmd_box.full) {
    bool is_ack = !strncmp(g_cmd_box.buf, "ACK:", 4);
    uint32_t acked = is_ack ? (uint32_t)strtoul(g_cmd_box.buf + 4, nullptr, 10) : 0;
    g_core->command(g_cmd_box.buf, now, reply, sizeof reply);
    g_cmd_box.full = false;
    if (is_ack && acked == g_inflight_id) g_inflight_id = 0;  // send the next one right away
    if (!is_ack) g_svc->cmd->setValue(String(reply));          // ACKs are not echoed (too chatty)
  }
}

}  // namespace

void begin(Core& core, const char* name) {
  g_core = &core;
  BLE.begin(name);
  g_svc = new Service();
  BLE.server()->addService(g_svc);
  BLE.server()->setCallbacks(g_svc);
  publishConfig();
  BLE.startAdvertising();
}

bool connected() { return g_svc && g_svc->is_connected; }
uint32_t lastDeliveryFailMs() { return g_fail_ms; }

void notifyLive(uint32_t now_ms) {
  if (!g_svc) return;
  char buf[200];
  size_t n = g_core->liveJson(buf, sizeof buf);
  if (n) g_svc->live->setValue((const uint8_t*)buf, n);
}

void poll(uint32_t now) {
  if (!g_svc) return;
  handleMailboxes(now);

  if (g_svc->just_connected) {
    g_svc->just_connected = false;
    g_inflight_id = 0;  // re-send whatever is pending
    publishConfig();
  }

  const Event* e = g_core->log.pending() ? nextToSend() : nullptr;
  bool emergency_pending = e && (e->type == EventType::EMERGENCY || e->type == EventType::MANUAL_SOS);

  if (!connected()) {
    // Section 4.3: if the phone is not connected the device keeps alerting
    // (the state machine does that) and the failure is logged.
    if (emergency_pending && !g_fail_logged) {
      g_fail_logged = true;
      g_fail_ms = now;
      Serial.printf("EMERGENCY %lu not delivered: phone not connected (retrying every %lus)\n",
                    (unsigned long)e->id, (unsigned long)(kResendMs / 1000));
    }
    return;
  }
  if (!e) {
    g_inflight_id = 0;
    g_fail_logged = false;
    return;
  }
  // Stop-and-wait: one event in flight until ACKed, re-sent every 10 s.
  // A newer EMERGENCY jumps ahead of an in-flight ordinary event.
  bool resend = g_inflight_id == e->id && g_core->log.isPending(g_inflight_id);
  bool due = resend ? now - g_inflight_ms >= kResendMs : now - g_last_send_ms >= kNextGapMs;
  if (!due) return;
  size_t n = EventLog::toJson(*e, g_json, sizeof g_json);
  if (!n) return;
  g_svc->event->setValue((const uint8_t*)g_json, n);
  g_inflight_ms = now;
  g_inflight_id = e->id;
  g_last_send_ms = now;
}

}  // namespace ble
}  // namespace ag
