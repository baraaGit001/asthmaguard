#include "storage.h"

#include <ArduinoJson.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <vector>

namespace ag {
namespace storage {

namespace {
std::string g_dir = "/var/lib/asthmaguard";

bool readFile(const std::string& path, std::string& out) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return false;
  char buf[4096];
  size_t n;
  out.clear();
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  fclose(f);
  return true;
}

bool writeAtomic(const std::string& path, const char* data, size_t len) {
  std::string tmp = path + ".tmp";
  int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) return false;
  bool ok = ::write(fd, data, len) == (ssize_t)len && fsync(fd) == 0;
  ::close(fd);
  if (!ok || rename(tmp.c_str(), path.c_str()) != 0) {
    unlink(tmp.c_str());
    return false;
  }
  int dfd = ::open(g_dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);  // persist the rename
  if (dfd >= 0) {
    fsync(dfd);
    ::close(dfd);
  }
  return true;
}
}  // namespace

bool init(const std::string& dir) {
  g_dir = dir;
  mkdir(dir.c_str(), 0755);
  return access(dir.c_str(), W_OK) == 0;
}

bool loadConfig(Config& c) {
  std::string s;
  if (!readFile(g_dir + "/config.json", s)) return false;
  return c.fromJson(s.data(), s.size(), nullptr);
}

bool saveConfig(const Config& c) {
  char buf[2048];
  size_t n = c.toJson(buf, sizeof buf);
  return n && writeAtomic(g_dir + "/config.json", buf, n);
}

bool loadEvents(EventLog& log) {
  std::string s;
  if (!readFile(g_dir + "/events.json", s)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, s)) return false;
  uint32_t next = doc["next_id"] | 1u;
  for (JsonObject o : doc["events"].as<JsonArray>()) {
    Event e;
    e.id = o["id"] | 0u;
    e.t = o["t"] | 0u;
    e.up_s = 0;  // uptime restarted with the reboot
    if (!e.id || !parseEventType(o["type"] | "", e.type)) continue;
    std::string data;
    serializeJson(o["data"], data);
    if (data.size() >= sizeof(e.data)) data = "{}";
    strcpy(e.data, data.c_str());
    log.restore(e);
  }
  // Ids must never repeat (the phone de-duplicates by id): skip a few in
  // case events were sent after the last save.
  if (next + 16 > log.nextId()) log.setNextId(next + 16);
  return true;
}

bool saveEvents(const EventLog& log) {
  std::string out = "{\"next_id\":" + std::to_string(log.nextId()) + ",\"events\":[";
  char buf[256];
  for (size_t i = 0; i < log.pending(); i++) {
    size_t n = EventLog::toJson(*log.at(i), buf, sizeof buf);
    if (!n) continue;
    if (i) out += ',';
    out.append(buf, n);
  }
  out += "]}";
  return writeAtomic(g_dir + "/events.json", out.data(), out.size());
}

}  // namespace storage
}  // namespace ag
