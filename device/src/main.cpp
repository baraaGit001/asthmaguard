// asthmaguardd: AsthmaGuard on the Raspberry Pi Zero 2 W.
//
// Main thread: one poll() loop -> sensors (PPG FIFO, IMU 50 Hz), button,
//   detector + state machine (1 Hz), actuators, BLE (BlueZ over D-Bus),
//   console. All logic runs here, so it never runs on two threads at once.
// Audio thread (hw/audio_alsa.*): I2S mic -> features -> cough model every
//   250 ms; scores come back through a queue.
//
// Console (same commands as the BLE `cmd` characteristic plus STATUS,
// CONFIG?, EVENTS, VERBOSE:0|1, SELFTEST, HELP):
//   sudo nc -U /run/asthmaguard/console        (running service)
//   asthmaguardd --state-dir /tmp/ag            (by hand, type on stdin)
//
// Wiring (BCM GPIO): I2C1 SDA 2 / SCL 3 (MAX30102 0x57, MPU6050 0x68),
// I2S BCLK 18 / LRCLK 19 / DIN 20 (INMP441), servo PWM0 on GPIO12,
// buzzer 23, vibration 24, button 17 to GND. Powered by a USB supply.
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "actuators.h"
#include "ble/bluez_gatt.h"
#include "button.h"
#include "console.h"
#include "core.h"
#include "cough_model.h"
#include "hw/audio_alsa.h"
#include "hw/gpio.h"
#include "hw/i2c.h"
#include "hw/max30102.h"
#include "hw/mpu6050.h"
#include "storage.h"

#ifndef AG_VERSION
#define AG_VERSION "dev"
#endif

using namespace ag;

namespace {

struct Options {
  std::string state_dir = "/var/lib/asthmaguard";
  std::string socket = "/run/asthmaguard/console";
  std::string i2c = "/dev/i2c-1";
  std::string gpiochip = "/dev/gpiochip0";
  std::string pcm = "plughw:CARD=sndrpigooglevoi,DEV=0";
  std::string name = "AsthmaGuard";
  int pwm_chip = 0, pwm_channel = 0;
  unsigned pin_buzzer = 23, pin_vib = 24, pin_button = 17;
  bool no_ble = false, selftest = false;
};

Options opt;
Core core;
I2cBus i2c;
Mpu6050 imu(core.cfg);
Max30102 ppg;
Actuators act(core.cfg);
Button button(core.cfg);
GpioLine button_line;

volatile sig_atomic_t g_stop = 0;
bool g_verbose = false, g_dose_in_progress = false;
uint32_t g_saved_version = ~0u, g_saved_next_id = 0;
PpgResult g_ppg;

uint32_t nowMs() { return audio::monoMs(); }

// ------------------------------------------------------------------ clock
// The Zero has no RTC. If NTP already set the system clock (Wi-Fi), use it;
// otherwise the phone sets the time over BLE on every connect.
void useSystemClockIfValid() {
  time_t t = time(nullptr);
  if (t > 1735689600) {  // 2025-01-01: the clock has been set
    char reply[64];
    char cmd[32];
    snprintf(cmd, sizeof cmd, "TIME:%lld", (long long)t);
    core.command(cmd, nowMs(), reply, sizeof reply);
  }
}

// ------------------------------------------------------------------ console
void printStatus() {
  audio::Stats a = audio::stats();
  console::printf("asthmaguardd %s state=%s alarm=%d episode=%d doses=%d dose_enabled=%d sim=%d pending=%u\n",
                  AG_VERSION, toString(core.sm.state()), (int)core.sm.alarm(), core.sm.episodeActive(),
                  core.sm.dosesThisEpisode(), core.cfg.doseLimitsSet(), core.sim.enabled(),
                  (unsigned)core.log.pending());
  console::printf("ble=%s connected=%d subscribed=%d | ppg=%d imu=%d i2c_errors=%u | mic=%d model=%d %s | time=%lu\n",
                  ble::status(), ble::connected(), ble::subscribed(), ppg.ok(), imu.ok(), (unsigned)i2c.errors(),
                  a.mic_ok, a.model_ok, a.error.c_str(), (unsigned long)core.clock.epochAt(nowMs()));
}

void printVerbose(uint32_t now) {
  audio::Stats a = audio::stats();
  console::broadcast(
      "[%6lus] hr=%5.1f spo2=%5.1f q=%d(%s) still=%d valid=%d ir=%lu red=%lu pi=%.4f R=%.2f | "
      "ax=%.2f ay=%.2f az=%.2f pitch=%6.1f act=%s std=%.3f | mic=%.1fdBFS inf=%lu/%lu %luus ovr=%lu | c60=%d "
      "st=%s\n",
      (unsigned long)(now / 1000), g_ppg.hr, g_ppg.spo2, g_ppg.quality_ok, g_ppg.why, imu.stillFor5s(),
      core.sig.ppg_valid, (unsigned long)ppg.lastIr(), (unsigned long)ppg.lastRed(), g_ppg.pi, g_ppg.r, imu.ax(),
      imu.ay(), imu.az(), imu.pitch(), toString(imu.activity()), imu.motionStd(), a.level_dbfs,
      (unsigned long)a.inferences, (unsigned long)a.silent_skips, (unsigned long)a.last_us,
      (unsigned long)a.overruns, core.sig.cough_60s, toString(core.sm.state()));
}

void handleLine(const std::string& l) {
  const char* line = l.c_str();
  uint32_t now = nowMs();
  if (!strcmp(line, "HELP")) {
    console::printf(
        "STATUS CONFIG? EVENTS VERBOSE:0|1 SELFTEST TEST_ALERT TEST_SERVO ACK:<id> TIME:<epoch> CONFIG:{json} "
        "SIM:ON|OFF|clear|hr=|spo2=|valid=|cough=|act=|raise|btn=short|long|sos\n");
  } else if (!strcmp(line, "STATUS")) {
    printStatus();
  } else if (!strcmp(line, "CONFIG?")) {
    static char buf[1536];
    if (core.cfg.toJson(buf, sizeof buf)) console::printf("%s\n", buf);
  } else if (!strcmp(line, "EVENTS")) {
    char buf[256];
    for (size_t i = 0; i < core.log.pending(); i++)
      if (EventLog::toJson(*core.log.at(i), buf, sizeof buf)) console::printf("%s\n", buf);
    console::printf("%u pending, %lu dropped\n", (unsigned)core.log.pending(), (unsigned long)core.log.dropped());
  } else if (!strncmp(line, "VERBOSE:", 8)) {
    g_verbose = line[8] == '1';
  } else if (!strcmp(line, "SELFTEST")) {
    audio::requestSelfTest();
    audio::SelfTest r;
    for (int i = 0; i < 1000 && !(r = audio::selfTest()).done; i++) usleep(10000);
    console::printf("model self-test: %d/%d exact, %d off by one LSB, %lu us/inference, model=%s arena=%lu\n",
                    r.exact, r.total, r.off_by_one, (unsigned long)r.us_per_inference, cough::status(),
                    (unsigned long)cough::arenaUsed());
  } else {
    char reply[128];
    core.command(line, now, reply, sizeof reply);
    console::printf("%s\n", reply);
  }
}

// ------------------------------------------------------------------ persistence
void persist() {
  if (core.takeConfigChanged() && !storage::saveConfig(core.cfg)) fprintf(stderr, "config save failed\n");
  // Pending events + id counter: saved whenever they change, so an
  // EMERGENCY not yet ACKed survives a pulled USB cable.
  if (core.log.version() != g_saved_version || core.log.nextId() != g_saved_next_id) {
    if (storage::saveEvents(core.log)) {
      g_saved_version = core.log.version();
      g_saved_next_id = core.log.nextId();
    }
  }
}

// ------------------------------------------------------------------ setup
void parseArgs(int argc, char** argv) {
  static option longopts[] = {{"state-dir", required_argument, nullptr, 's'},
                              {"socket", required_argument, nullptr, 'S'},
                              {"i2c", required_argument, nullptr, 'i'},
                              {"gpiochip", required_argument, nullptr, 'g'},
                              {"pcm", required_argument, nullptr, 'p'},
                              {"name", required_argument, nullptr, 'n'},
                              {"no-ble", no_argument, nullptr, 'B'},
                              {"selftest", no_argument, nullptr, 'T'},
                              {"version", no_argument, nullptr, 'V'},
                              {"help", no_argument, nullptr, 'h'},
                              {nullptr, 0, nullptr, 0}};
  int c;
  while ((c = getopt_long(argc, argv, "", longopts, nullptr)) != -1) {
    switch (c) {
      case 's': opt.state_dir = optarg; break;
      case 'S': opt.socket = optarg; break;
      case 'i': opt.i2c = optarg; break;
      case 'g': opt.gpiochip = optarg; break;
      case 'p': opt.pcm = optarg; break;
      case 'n': opt.name = optarg; break;
      case 'B': opt.no_ble = true; break;
      case 'T': opt.selftest = true; break;
      case 'V': printf("asthmaguardd %s\n", AG_VERSION); exit(0);
      default:
        printf("usage: asthmaguardd [--state-dir DIR] [--socket PATH|''] [--i2c DEV] [--gpiochip DEV]\n"
               "                    [--pcm ALSA_PCM] [--name BLE_NAME] [--no-ble] [--selftest] [--version]\n");
        exit(c == 'h' ? 0 : 2);
    }
  }
}

// --selftest: check every part once and exit (0 = all OK). For bring-up.
int runSelfTest() {
  int fails = 0;
  auto line = [&](const char* what, bool ok, const char* detail) {
    printf("%-28s %s %s\n", what, ok ? "OK  " : "FAIL", detail ? detail : "");
    if (!ok) fails++;
  };
  bool bus = i2c.ok();
  line("I2C bus", bus, opt.i2c.c_str());
  char d[64];
  snprintf(d, sizeof d, "WHO_AM_I=0x%02X", imu.whoAmI());
  line("MPU6050 (0x68)", imu.ok(), d);
  snprintf(d, sizeof d, "PART_ID=0x%02X", ppg.partId());
  line("MAX30102 (0x57)", ppg.ok(), d);
  line("button GPIO", button_line.ok(), opt.gpiochip.c_str());
  bool model = cough::init();
  line("cough model", model, cough::status());
  audio::requestSelfTest();
  audio::SelfTest r = audio::selfTest();
  snprintf(d, sizeof d, "%d/%d exact, %d off by 1, %u us", r.exact, r.total, r.off_by_one,
           (unsigned)r.us_per_inference);
  line("model vs TFLite reference", r.total > 0 && r.exact + r.off_by_one == r.total, d);
  return fails ? 1 : 0;
}

void onSignal(int) { g_stop = 1; }

}  // namespace

int main(int argc, char** argv) {
  parseArgs(argc, argv);
  setvbuf(stdout, nullptr, _IOLBF, 0);
  signal(SIGPIPE, SIG_IGN);
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = onSignal;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);

  // Missing hardware is reported, never fatal: the logic, BLE and the
  // simulation keep working, so the app can be tested without sensors.
  if (!i2c.open(opt.i2c.c_str())) fprintf(stderr, "I2C %s: cannot open (dtparam=i2c_arm=on?)\n", opt.i2c.c_str());
  if (i2c.ok() && !imu.begin(i2c)) fprintf(stderr, "MPU6050 not found\n");
  if (i2c.ok() && !ppg.begin(i2c)) fprintf(stderr, "MAX30102 not found\n");
  if (!button_line.openInputPullUp(opt.gpiochip.c_str(), opt.pin_button)) fprintf(stderr, "button GPIO unavailable\n");

  if (opt.selftest) return runSelfTest();

  if (!storage::init(opt.state_dir)) fprintf(stderr, "state dir %s not writable\n", opt.state_dir.c_str());
  if (!storage::loadConfig(core.cfg)) fprintf(stderr, "config: defaults\n");
  storage::loadEvents(core.log);
  g_saved_version = core.log.version();
  g_saved_next_id = core.log.nextId();

  int fail = act.begin(opt.gpiochip.c_str(), opt.pin_buzzer, opt.pin_vib, opt.pwm_chip, opt.pwm_channel);
  if (fail & 1) fprintf(stderr, "buzzer GPIO unavailable\n");
  if (fail & 2) fprintf(stderr, "vibration GPIO unavailable\n");
  if (fail & 4) fprintf(stderr, "servo PWM unavailable (dtoverlay=pwm,pin=12,func=4?) - puffs disabled\n");

  if (!audio::start(opt.pcm, -75.0f)) fprintf(stderr, "microphone: %s\n", audio::stats().error.c_str());
  if (!audio::stats().model_ok) fprintf(stderr, "cough model: %s\n", cough::status());

  useSystemClockIfValid();
  if (!opt.no_ble && !ble::begin(core, opt.name.c_str())) fprintf(stderr, "BLE: %s\n", ble::status());
  if (!console::begin(opt.socket, isatty(STDIN_FILENO), handleLine))
    fprintf(stderr, "console socket %s unavailable\n", opt.socket.c_str());

  core.log.add(EventType::BOOT, 0, 0, "{\"fw\":\"" AG_VERSION "\"}");
  printf("AsthmaGuard %s ready.%s\n", AG_VERSION, isatty(STDIN_FILENO) ? " Type HELP." : "");

  uint32_t next_second = nowMs() + 1000;
  std::vector<pollfd> fds;
  while (!g_stop) {
    // Wait for D-Bus / console input, at most 5 ms (button + IMU timing).
    fds.clear();
    if (ble::fd() >= 0) fds.push_back({ble::fd(), POLLIN, 0});
    console::addFds(fds);
    poll(fds.data(), fds.size(), 5);
    uint32_t now = nowMs();

    ppg.poll();
    if (imu.poll(now)) core.onArmRaise(now);

    ButtonEvent be = button.update(button_line.ok() && !button_line.get(), now);
    if (button.takeLongReached()) act.tick();
    if (be != ButtonEvent::NONE) core.onButton(be, now);

    audio::Score sc;
    while (audio::popScore(sc)) core.onCoughScore(sc.p, sc.t_ms);

    console::handle(fds);
    ble::process(now);

    if ((int32_t)(now - next_second) >= 0) {
      next_second += 1000;
      if ((int32_t)(now - next_second) > 3000) next_second = now + 1000;  // fell far behind
      imu.endSecond();
      g_ppg = ppg.compute();
      SensorSample s;
      s.hr = g_ppg.hr;
      s.spo2 = g_ppg.spo2;
      // Invalid readings are never used: the signal must pass the quality
      // check AND the wrist must have been still for the last 5 s.
      s.ppg_valid = g_ppg.quality_ok && imu.stillFor5s();
      s.activity = imu.activity();
      s.battery = kBatteryUnknown;  // USB powered, no battery
      core.secondTick(now, s);
      ble::notifyLive(now);
      if (g_verbose) printVerbose(now);
    }

    act.setAlarm(core.sm.alarm());
    if (core.sm.takePuffRequest()) {
      g_dose_in_progress = act.startPuff();
      if (!g_dose_in_progress) fprintf(stderr, "puff refused: servo busy or unavailable\n");
    }
    if (core.takeTestServo()) act.testServo();
    if (core.takeTestAlert()) act.testAlert();
    if (act.update(now) && g_dose_in_progress) {
      g_dose_in_progress = false;
      core.onPuffDone(now);
    }
    persist();
  }

  printf("stopping\n");
  persist();
  act.setAlarm(Alarm::NONE);
  act.update(nowMs());
  audio::stop();
  ble::end();
  console::end();
  return 0;
}
