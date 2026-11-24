#include "hw/max30102.h"

#include <time.h>

namespace ag {

namespace {
const uint8_t REG_FIFO_WR = 0x04, REG_OVF = 0x05, REG_FIFO_RD = 0x06, REG_FIFO_DATA = 0x07,
              REG_FIFO_CFG = 0x08, REG_MODE = 0x09, REG_SPO2_CFG = 0x0A, REG_LED1 = 0x0C, REG_LED2 = 0x0D,
              REG_PART_ID = 0xFF;
const uint8_t kPartId = 0x15;

void sleepMs(int ms) {
  timespec ts = {0, ms * 1000000L};
  nanosleep(&ts, nullptr);
}
}  // namespace

bool Max30102::begin(I2cBus& bus, uint8_t addr) {
  bus_ = &bus;
  addr_ = addr;
  if (!bus_->readReg(addr_, REG_PART_ID, part_) || part_ != kPartId) return ok_ = false;
  bus_->writeReg(addr_, REG_MODE, 0x40);  // reset
  sleepMs(10);
  ok_ = bus_->writeReg(addr_, REG_FIFO_CFG, 0x1F)      // no averaging, rollover on, almost-full 15
        && bus_->writeReg(addr_, REG_SPO2_CFG, 0x67)   // 16384 nA range, 100 Hz, 411 us (18 bit)
        && bus_->writeReg(addr_, REG_LED1, 0x3F)       // red ~12.5 mA (wrist needs more light than a finger)
        && bus_->writeReg(addr_, REG_LED2, 0x3F)       // IR
        && bus_->writeReg(addr_, REG_FIFO_WR, 0) && bus_->writeReg(addr_, REG_OVF, 0) &&
        bus_->writeReg(addr_, REG_FIFO_RD, 0)
        && bus_->writeReg(addr_, REG_MODE, 0x03);      // SpO2 mode (red + IR)
  return ok_;
}

void Max30102::poll() {
  if (!ok_) return;
  uint8_t wr = 0, rd = 0, ovf = 0;
  if (!bus_->readReg(addr_, REG_FIFO_WR, wr) || !bus_->readReg(addr_, REG_FIFO_RD, rd)) return;
  bus_->readReg(addr_, REG_OVF, ovf);
  if (ovf & 0x1F) overflows_ += ovf & 0x1F;
  int n = (wr - rd) & 0x1F;
  if (n == 0 && (ovf & 0x1F)) n = 32;  // full FIFO wrapped
  uint8_t buf[32 * 6];
  if (n == 0 || !bus_->readRegs(addr_, REG_FIFO_DATA, buf, (size_t)n * 6)) return;
  for (int i = 0; i < n; i++) {
    const uint8_t* s = buf + i * 6;
    last_red_ = (((uint32_t)s[0] << 16) | ((uint32_t)s[1] << 8) | s[2]) & 0x3FFFF;
    last_ir_ = (((uint32_t)s[3] << 16) | ((uint32_t)s[4] << 8) | s[5]) & 0x3FFFF;
    algo_.add(last_red_, last_ir_);
    samples_++;
  }
}

}  // namespace ag
