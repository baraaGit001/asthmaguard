// Text console: stdin (when run by hand) and a Unix socket for the running
// service:  nc -U /run/asthmaguard/console   (then type HELP)
// One line in -> handler -> reply lines out to the same client.
#pragma once
#include <poll.h>

#include <functional>
#include <string>
#include <vector>

namespace ag {
namespace console {

using Handler = std::function<void(const std::string& line)>;

bool begin(const std::string& socket_path, bool use_stdin, Handler h);
// Adds the console fds to a poll set; call handle() with the same set after poll().
void addFds(std::vector<pollfd>& fds);
void handle(const std::vector<pollfd>& fds);
// Output to the client whose line is being handled (stdout otherwise).
void printf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
// Output to stdout/journal and to every connected socket client (VERBOSE lines, alerts).
void broadcast(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void end();

}  // namespace console
}  // namespace ag
