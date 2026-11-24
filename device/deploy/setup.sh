#!/bin/sh
# Install AsthmaGuard on a Raspberry Pi Zero 2 W (Raspberry Pi OS Bookworm, 64-bit
# recommended; 32-bit also builds). Run from the repo root:   sudo sh device/deploy/setup.sh
set -eu
[ "$(id -u)" = 0 ] || { echo "run with sudo"; exit 1; }
cd "$(dirname "$0")/.."

echo "== packages"
apt-get update
apt-get install -y --no-install-recommends build-essential cmake pkg-config libsystemd-dev libasound2-dev \
  bluez netcat-openbsd i2c-tools alsa-utils

echo "== boot config"
CFG=/boot/firmware/config.txt
[ -f "$CFG" ] || CFG=/boot/config.txt
if ! grep -q "AsthmaGuard" "$CFG"; then
  cp "$CFG" "$CFG.before-asthmaguard"
  cat deploy/config.txt.snippet >> "$CFG"
  REBOOT=1
fi

echo "== build (takes ~10 min on the Zero 2 W the first time)"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
(cd build && ctest --output-on-failure)
install -m 755 build/asthmaguardd /usr/local/bin/asthmaguardd
install -m 644 deploy/asthmaguard.service /etc/systemd/system/asthmaguard.service

echo "== bluetooth: always on, LE only is fine"
systemctl enable --now bluetooth
rfkill unblock bluetooth || true

systemctl daemon-reload
systemctl enable asthmaguard
if [ "${REBOOT:-0}" = 1 ]; then
  echo "Boot config changed: reboot now (sudo reboot). The service starts on boot."
else
  systemctl restart asthmaguard
  echo "Running. Check: sudo asthmaguardd --selftest   |   journalctl -u asthmaguard -f"
fi
