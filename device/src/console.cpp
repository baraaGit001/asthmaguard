#include "console.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace ag {
namespace console {

namespace {
struct Client {
  int fd;
  std::string buf;
};
Handler g_h;
int g_listen = -1;
bool g_stdin = false;
std::string g_stdin_buf, g_path;
std::vector<Client> g_clients;
int g_out = STDOUT_FILENO;  // where printf() goes right now
const size_t kMaxLine = 2048;

void writeAll(int fd, const char* s, size_t n) {
  while (n) {
    ssize_t w = ::write(fd, s, n);
    if (w < 0 && errno == EINTR) continue;
    if (w <= 0) return;  // gone or not reading (non-blocking): drop, never stall the loop
    s += w;
    n -= (size_t)w;
  }
}

// Feeds bytes; runs the handler per complete line. False = line too long.
bool feed(std::string& buf, const char* data, size_t n, int out) {
  for (size_t i = 0; i < n; i++) {
    char c = data[i];
    if (c == '\r') continue;
    if (c == '\n') {
      if (!buf.empty()) {
        g_out = out;
        g_h(buf);
        g_out = STDOUT_FILENO;
      }
      buf.clear();
    } else if (buf.size() < kMaxLine) {
      buf += c;
    } else {
      buf.clear();
      return false;
    }
  }
  return true;
}
}  // namespace

bool begin(const std::string& path, bool use_stdin, Handler h) {
  g_h = std::move(h);
  g_stdin = use_stdin;
  g_path = path;
  if (path.empty()) return true;
  g_listen = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (g_listen < 0) return false;
  sockaddr_un a;
  memset(&a, 0, sizeof a);
  a.sun_family = AF_UNIX;
  if (path.size() >= sizeof a.sun_path) return false;
  strcpy(a.sun_path, path.c_str());
  unlink(path.c_str());
  if (bind(g_listen, (sockaddr*)&a, sizeof a) < 0 || listen(g_listen, 2) < 0) {
    ::close(g_listen);
    g_listen = -1;
    return false;
  }
  chmod(path.c_str(), 0660);  // root + group (see the systemd unit)
  return true;
}

void addFds(std::vector<pollfd>& fds) {
  if (g_stdin) fds.push_back({STDIN_FILENO, POLLIN, 0});
  if (g_listen >= 0) fds.push_back({g_listen, POLLIN, 0});
  for (Client& c : g_clients) fds.push_back({c.fd, POLLIN, 0});
}

void handle(const std::vector<pollfd>& fds) {
  char buf[512];
  for (const pollfd& p : fds) {
    if (!(p.revents & (POLLIN | POLLHUP | POLLERR))) continue;
    if (g_stdin && p.fd == STDIN_FILENO) {
      ssize_t n = ::read(STDIN_FILENO, buf, sizeof buf);
      if (n <= 0) g_stdin = false;  // EOF (e.g. under systemd): stop reading stdin
      else feed(g_stdin_buf, buf, (size_t)n, STDOUT_FILENO);
    } else if (p.fd == g_listen) {
      int fd = accept4(g_listen, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
      if (fd < 0) continue;
      if (g_clients.size() >= 4) {
        ::close(fd);
        continue;
      }
      g_clients.push_back({fd, {}});
      const char* hi = "AsthmaGuard console. Type HELP.\n";
      writeAll(fd, hi, strlen(hi));
    } else {
      for (size_t i = 0; i < g_clients.size(); i++) {
        if (g_clients[i].fd != p.fd) continue;
        ssize_t n = ::read(p.fd, buf, sizeof buf);
        if (n <= 0 || !feed(g_clients[i].buf, buf, (size_t)n, p.fd)) {
          ::close(p.fd);
          g_clients.erase(g_clients.begin() + (long)i);
        }
        break;
      }
    }
  }
}

void printf(const char* fmt, ...) {
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  writeAll(g_out, buf, (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1);
}

void broadcast(const char* fmt, ...) {
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (n <= 0) return;
  size_t len = (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1;
  writeAll(STDOUT_FILENO, buf, len);
  for (Client& c : g_clients) writeAll(c.fd, buf, len);
}

void end() {
  for (Client& c : g_clients) ::close(c.fd);
  g_clients.clear();
  if (g_listen >= 0) {
    ::close(g_listen);
    unlink(g_path.c_str());
  }
  g_listen = -1;
}

}  // namespace console
}  // namespace ag
