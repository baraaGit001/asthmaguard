#include "ble/bluez_gatt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <systemd/sd-bus.h>

#include <string>

namespace ag {
namespace ble {

namespace {

#define AG_UUID(n) "a5f1000" n "-7c1e-4b9a-9d3e-6a1f00000000"
const char* kAppPath = "/org/asthmaguard";
const char* kSvcPath = "/org/asthmaguard/service0";
const char* kAdvPath = "/org/asthmaguard_adv/adv0";
const char* kChrIface = "org.bluez.GattCharacteristic1";

const uint32_t kResendMs = 10000;  // un-ACKed event is re-sent after this
const uint32_t kNextGapMs = 150;   // pacing between two different events

enum ChrId { LIVE, EVENT, CONFIG, TIME, CMD, NCHR };

struct Chr {
  ChrId id;
  const char* uuid;
  const char* flags[4];
  std::string path;
  std::string value;
  bool notifying = false;
};

Chr g_chr[NCHR] = {
    {LIVE, AG_UUID("1"), {"read", "notify", nullptr}, {}, "{}", false},
    {EVENT, AG_UUID("2"), {"read", "notify", "indicate", nullptr}, {}, "{}", false},
    {CONFIG, AG_UUID("3"), {"read", "write", nullptr}, {}, "{}", false},
    {TIME, AG_UUID("4"), {"write", nullptr}, {}, "", false},
    {CMD, AG_UUID("5"), {"write", "notify", nullptr}, {}, "", false},
};

sd_bus* g_bus = nullptr;
Core* g_core = nullptr;
std::string g_adapter, g_name, g_status = "not started";
int g_connected = 0;  // connected Device1 objects
bool g_app_ok = false, g_adv_ok = false;
uint32_t g_now = 0;

uint32_t g_inflight_id = 0, g_inflight_ms = 0, g_last_send_ms = 0;
bool g_fail_logged = false, g_resync = false;

void setValue(Chr& c, const char* data, size_t n) {
  c.value.assign(data, n);
  if (c.notifying || c.id == CONFIG || c.id == EVENT || c.id == LIVE)
    sd_bus_emit_properties_changed(g_bus, c.path.c_str(), kChrIface, "Value", nullptr);
}
void setValue(Chr& c, const char* s) { setValue(c, s, strlen(s)); }

void publishConfig() {
  static char buf[1536];
  size_t n = g_core->cfg.toJson(buf, sizeof buf);
  if (n) setValue(g_chr[CONFIG], buf, n);
}

// --------------------------------------------------------------- writes
void onWrite(Chr& c, const std::string& v) {
  char reply[128];
  if (c.id == TIME) {
    std::string cmd = "TIME:" + v;
    g_core->command(cmd.c_str(), g_now, reply, sizeof reply);
    setValue(g_chr[CMD], reply);
  } else if (c.id == CONFIG) {
    std::string cmd = "CONFIG:" + v;
    g_core->command(cmd.c_str(), g_now, reply, sizeof reply);
    publishConfig();
    setValue(g_chr[CMD], reply);  // "OK config" or "ERR value out of range"
  } else if (c.id == CMD) {
    bool is_ack = !strncmp(v.c_str(), "ACK:", 4);
    uint32_t acked = is_ack ? (uint32_t)strtoul(v.c_str() + 4, nullptr, 10) : 0;
    g_core->command(v.c_str(), g_now, reply, sizeof reply);
    if (is_ack && acked == g_inflight_id) g_inflight_id = 0;  // send the next one right away
    if (!is_ack) setValue(g_chr[CMD], reply);                 // ACKs are not echoed (too chatty)
  }
}

// --------------------------------------------------------------- D-Bus: characteristic
int readOffset(sd_bus_message* m, uint16_t& offset) {
  offset = 0;
  int r = sd_bus_message_enter_container(m, 'a', "{sv}");
  if (r < 0) return r;
  while ((r = sd_bus_message_enter_container(m, 'e', "sv")) > 0) {
    const char* key;
    if ((r = sd_bus_message_read(m, "s", &key)) < 0) return r;
    if (!strcmp(key, "offset")) {
      if ((r = sd_bus_message_read(m, "v", "q", &offset)) < 0) return r;
    } else if ((r = sd_bus_message_skip(m, "v")) < 0) {
      return r;
    }
    if ((r = sd_bus_message_exit_container(m)) < 0) return r;
  }
  if (r < 0) return r;
  return sd_bus_message_exit_container(m);
}

int mReadValue(sd_bus_message* m, void* ud, sd_bus_error* err) {
  Chr& c = *(Chr*)ud;
  uint16_t off;
  if (readOffset(m, off) < 0) off = 0;
  if (off > c.value.size()) return sd_bus_error_set(err, "org.bluez.Error.InvalidOffset", "offset");
  sd_bus_message* reply;
  int r = sd_bus_message_new_method_return(m, &reply);
  if (r < 0) return r;
  r = sd_bus_message_append_array(reply, 'y', c.value.data() + off, c.value.size() - off);
  if (r >= 0) r = sd_bus_send(nullptr, reply, nullptr);
  sd_bus_message_unref(reply);
  return r;
}

int mWriteValue(sd_bus_message* m, void* ud, sd_bus_error* err) {
  Chr& c = *(Chr*)ud;
  const void* p;
  size_t n;
  int r = sd_bus_message_read_array(m, 'y', &p, &n);
  if (r < 0) return r;
  uint16_t off;
  readOffset(m, off);
  if (off != 0) return sd_bus_error_set(err, "org.bluez.Error.InvalidOffset", "no prepared writes");
  if (n > 1600) return sd_bus_error_set(err, "org.bluez.Error.InvalidValueLength", "too long");
  std::string v((const char*)p, n);
  while (!v.empty() && (v.back() == 0 || v.back() == '\n' || v.back() == '\r')) v.pop_back();
  onWrite(c, v);
  return sd_bus_reply_method_return(m, "");
}

int mStartNotify(sd_bus_message* m, void* ud, sd_bus_error*) {
  Chr& c = *(Chr*)ud;
  if (!c.notifying) {
    c.notifying = true;
    sd_bus_emit_properties_changed(g_bus, c.path.c_str(), kChrIface, "Notifying", nullptr);
    if (c.id == EVENT) g_resync = true;  // phone (re)subscribed: send pending events again
  }
  return sd_bus_reply_method_return(m, "");
}

int mStopNotify(sd_bus_message* m, void* ud, sd_bus_error*) {
  Chr& c = *(Chr*)ud;
  if (c.notifying) {
    c.notifying = false;
    sd_bus_emit_properties_changed(g_bus, c.path.c_str(), kChrIface, "Notifying", nullptr);
  }
  return sd_bus_reply_method_return(m, "");
}

int pUuid(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* ud, sd_bus_error*) {
  return sd_bus_message_append(reply, "s", ((Chr*)ud)->uuid);
}
int pService(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
  return sd_bus_message_append(reply, "o", kSvcPath);
}
int pFlags(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* ud, sd_bus_error*) {
  return sd_bus_message_append_strv(reply, (char**)((Chr*)ud)->flags);
}
int pValue(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* ud, sd_bus_error*) {
  Chr& c = *(Chr*)ud;
  return sd_bus_message_append_array(reply, 'y', c.value.data(), c.value.size());
}
int pNotifying(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void* ud, sd_bus_error*) {
  return sd_bus_message_append(reply, "b", (int)((Chr*)ud)->notifying);
}

const sd_bus_vtable kChrVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID", "s", pUuid, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Service", "o", pService, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Flags", "as", pFlags, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Value", "ay", pValue, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Notifying", "b", pNotifying, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_METHOD("ReadValue", "a{sv}", "ay", mReadValue, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("WriteValue", "aya{sv}", "", mWriteValue, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("StartNotify", "", "", mStartNotify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_METHOD("StopNotify", "", "", mStopNotify, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END};

// --------------------------------------------------------------- D-Bus: service
int pSvcUuid(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
  return sd_bus_message_append(reply, "s", AG_UUID("0"));
}
int pPrimary(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
  return sd_bus_message_append(reply, "b", 1);
}
const sd_bus_vtable kSvcVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("UUID", "s", pSvcUuid, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Primary", "b", pPrimary, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END};

// --------------------------------------------------------------- D-Bus: advertisement
int pAdvType(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
  return sd_bus_message_append(reply, "s", "peripheral");
}
int pAdvUuids(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
  return sd_bus_message_append(reply, "as", 1, AG_UUID("0"));
}
int pAdvName(sd_bus*, const char*, const char*, const char*, sd_bus_message* reply, void*, sd_bus_error*) {
  return sd_bus_message_append(reply, "s", g_name.c_str());
}
int mAdvRelease(sd_bus_message* m, void*, sd_bus_error*) {
  g_adv_ok = false;
  g_status = "advertisement released by BlueZ";
  return sd_bus_reply_method_return(m, "");
}
// 128-bit UUID (18 B) + flags (3 B) fill most of the 31-byte packet, so
// the name goes in the scan response (BlueZ does that when it does not fit).
const sd_bus_vtable kAdvVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("Type", "s", pAdvType, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ServiceUUIDs", "as", pAdvUuids, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("LocalName", "s", pAdvName, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_METHOD("Release", "", "", mAdvRelease, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_VTABLE_END};

// --------------------------------------------------------------- registration
int onRegisterApp(sd_bus_message* m, void*, sd_bus_error*) {
  const sd_bus_error* e = sd_bus_message_get_error(m);
  g_app_ok = !e;
  if (e) g_status = std::string("GATT register failed: ") + e->message;
  else if (g_adv_ok) g_status = "advertising";
  fprintf(stderr, "ble: GATT application %s\n", e ? e->message : "registered");
  return 0;
}

int onRegisterAdv(sd_bus_message* m, void*, sd_bus_error*) {
  const sd_bus_error* e = sd_bus_message_get_error(m);
  g_adv_ok = !e;
  if (e) g_status = std::string("advertising failed: ") + e->message;
  else if (g_app_ok) g_status = "advertising";
  fprintf(stderr, "ble: advertisement %s\n", e ? e->message : "registered");
  return 0;
}

void registerAll() {
  g_app_ok = g_adv_ok = false;
  g_status = "registering";
  // Power the adapter on and set the name the phone shows.
  sd_bus_call_method_async(g_bus, nullptr, "org.bluez", g_adapter.c_str(), "org.freedesktop.DBus.Properties", "Set",
                           nullptr, nullptr, "ssv", "org.bluez.Adapter1", "Powered", "b", 1);
  sd_bus_call_method_async(g_bus, nullptr, "org.bluez", g_adapter.c_str(), "org.freedesktop.DBus.Properties", "Set",
                           nullptr, nullptr, "ssv", "org.bluez.Adapter1", "Alias", "s", g_name.c_str());
  // Asynchronous on purpose: BlueZ calls GetManagedObjects on us before it
  // replies, which a blocking call could never answer.
  sd_bus_call_method_async(g_bus, nullptr, "org.bluez", g_adapter.c_str(), "org.bluez.GattManager1",
                           "RegisterApplication", onRegisterApp, nullptr, "oa{sv}", kAppPath, 0);
  sd_bus_call_method_async(g_bus, nullptr, "org.bluez", g_adapter.c_str(), "org.bluez.LEAdvertisingManager1",
                           "RegisterAdvertisement", onRegisterAdv, nullptr, "oa{sv}", kAdvPath, 0);
}

// Device1.Connected changes -> phone connected / disconnected.
int onPropsChanged(sd_bus_message* m, void*, sd_bus_error*) {
  const char* iface;
  if (sd_bus_message_read(m, "s", &iface) < 0 || strcmp(iface, "org.bluez.Device1")) return 0;
  if (sd_bus_message_enter_container(m, 'a', "{sv}") < 0) return 0;
  while (sd_bus_message_enter_container(m, 'e', "sv") > 0) {
    const char* key;
    if (sd_bus_message_read(m, "s", &key) < 0) return 0;
    if (!strcmp(key, "Connected")) {
      int on = 0;
      if (sd_bus_message_read(m, "v", "b", &on) < 0) return 0;
      g_connected += on ? 1 : -1;
      if (g_connected < 0) g_connected = 0;
      fprintf(stderr, "ble: phone %s\n", on ? "connected" : "disconnected");
      if (on) g_resync = true;
    } else {
      sd_bus_message_skip(m, "v");
    }
    sd_bus_message_exit_container(m);
  }
  return 0;
}

// bluetoothd restarted: register again.
int onNameOwner(sd_bus_message* m, void*, sd_bus_error*) {
  const char *name, *old_owner, *new_owner;
  if (sd_bus_message_read(m, "sss", &name, &old_owner, &new_owner) < 0) return 0;
  if (strcmp(name, "org.bluez")) return 0;
  g_connected = 0;
  for (Chr& c : g_chr) c.notifying = false;
  if (new_owner && *new_owner) {
    fprintf(stderr, "ble: bluetoothd (re)started, registering\n");
    registerAll();
  } else {
    g_status = "bluetoothd not running";
  }
  return 0;
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

void deliver(uint32_t now) {
  if (g_resync) {
    g_resync = false;
    g_inflight_id = 0;  // re-send whatever is pending
    publishConfig();
  }
  const Event* e = g_core->log.pending() ? nextToSend() : nullptr;
  bool emergency = e && (e->type == EventType::EMERGENCY || e->type == EventType::MANUAL_SOS);
  if (!g_chr[EVENT].notifying) {
    // Section 4.3: if the phone is not listening the device keeps alerting
    // (the state machine does that) and the failure is logged once.
    if (emergency && !g_fail_logged) {
      g_fail_logged = true;
      fprintf(stderr, "EMERGENCY %u not delivered: phone not connected\n", (unsigned)e->id);
    }
    return;
  }
  if (!e) {
    g_inflight_id = 0;
    g_fail_logged = false;
    return;
  }
  // Stop-and-wait: one event in flight until ACKed, re-sent every 10 s.
  bool resend = g_inflight_id == e->id && g_core->log.isPending(g_inflight_id);
  bool due = resend ? now - g_inflight_ms >= kResendMs : now - g_last_send_ms >= kNextGapMs;
  if (!due) return;
  char json[256];
  size_t n = EventLog::toJson(*e, json, sizeof json);
  if (!n) return;
  setValue(g_chr[EVENT], json, n);
  g_inflight_ms = now;
  g_inflight_id = e->id;
  g_last_send_ms = now;
}

}  // namespace

bool begin(Core& core, const char* name, const char* adapter) {
  g_core = &core;
  g_name = name;
  g_adapter = adapter;
  int r = sd_bus_open_system(&g_bus);
  if (r < 0) {
    g_status = std::string("system bus: ") + strerror(-r);
    g_bus = nullptr;
    return false;
  }
  sd_bus_add_object_manager(g_bus, nullptr, kAppPath);
  sd_bus_add_object_vtable(g_bus, nullptr, kSvcPath, "org.bluez.GattService1", kSvcVtable, nullptr);
  for (int i = 0; i < NCHR; i++) {
    g_chr[i].path = std::string(kSvcPath) + "/char" + std::to_string(i);
    sd_bus_add_object_vtable(g_bus, nullptr, g_chr[i].path.c_str(), kChrIface, kChrVtable, &g_chr[i]);
  }
  sd_bus_add_object_vtable(g_bus, nullptr, kAdvPath, "org.bluez.LEAdvertisement1", kAdvVtable, nullptr);
  sd_bus_match_signal(g_bus, nullptr, "org.bluez", nullptr, "org.freedesktop.DBus.Properties", "PropertiesChanged",
                      onPropsChanged, nullptr);
  sd_bus_match_signal(g_bus, nullptr, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                      "NameOwnerChanged", onNameOwner, nullptr);
  publishConfig();
  registerAll();
  return true;
}

int fd() { return g_bus ? sd_bus_get_fd(g_bus) : -1; }

int process(uint32_t now_ms) {
  if (!g_bus) return 0;
  g_now = now_ms;
  int r, n = 0;
  while ((r = sd_bus_process(g_bus, nullptr)) > 0) n++;
  if (r < 0) {
    g_status = std::string("bus error: ") + strerror(-r);
    return r;
  }
  deliver(now_ms);
  sd_bus_flush(g_bus);
  return n;
}

void notifyLive(uint32_t) {
  if (!g_bus) return;
  char buf[200];
  size_t n = g_core->liveJson(buf, sizeof buf);
  if (n) setValue(g_chr[LIVE], buf, n);
  sd_bus_flush(g_bus);
}

bool connected() { return g_connected > 0 || g_chr[EVENT].notifying; }
bool subscribed() { return g_chr[EVENT].notifying; }
const char* status() { return g_status.c_str(); }

void end() {
  if (!g_bus) return;
  sd_bus_call_method(g_bus, "org.bluez", g_adapter.c_str(), "org.bluez.LEAdvertisingManager1",
                     "UnregisterAdvertisement", nullptr, nullptr, "o", kAdvPath);
  sd_bus_call_method(g_bus, "org.bluez", g_adapter.c_str(), "org.bluez.GattManager1", "UnregisterApplication",
                     nullptr, nullptr, "o", kAppPath);
  sd_bus_flush_close_unref(g_bus);
  g_bus = nullptr;
}

}  // namespace ble
}  // namespace ag
