// Persistent state in a directory (default /var/lib/asthmaguard, created by
// systemd StateDirectory=): config.json and events.json (pending, un-ACKed
// events + next event id). Writes are atomic (temp file, fsync, rename), so
// pulling the USB cable never leaves a half-written file.
#pragma once
#include <string>

#include "config.h"
#include "eventlog.h"

namespace ag {
namespace storage {

bool init(const std::string& dir);
bool loadConfig(Config& c);
bool saveConfig(const Config& c);
// Restores pending events and the id counter. Events whose wall-clock time
// was never set keep t = 0 and get "now" when the phone sets the time.
bool loadEvents(EventLog& log);
bool saveEvents(const EventLog& log);

}  // namespace storage
}  // namespace ag
