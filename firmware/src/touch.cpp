#include "touch.h"
#include <Wire.h>
#include "board_pins.h"

namespace {
constexpr uint8_t REG_FINGER_NUM = 0x02;  // 0x02..0x06: count, xH, xL, yH, yL
constexpr uint8_t REG_CHIP_ID = 0xA7;

bool readRegs(uint8_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(CST816_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom((int)CST816_ADDR, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}
}  // namespace

bool Touch::begin() {
  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, LOW);
  delay(20);
  digitalWrite(PIN_TP_RST, HIGH);
  delay(80);  // the controller needs ~50ms after reset before it answers

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
  pinMode(PIN_TP_INT, INPUT_PULLUP);

  uint8_t id = 0;
  _present = readRegs(REG_CHIP_ID, &id, 1);
  if (_present) {
    Serial.printf("[touch] CST816 chip id 0x%02X\n", id);
  } else {
    Serial.println("[touch] not responding on I2C -- check PIN_I2C_SDA/SCL and PIN_TP_RST");
  }
  return _present;
}

bool Touch::read(int16_t &x, int16_t &y) {
  if (!_present) return false;
  uint8_t b[5];
  if (!readRegs(REG_FINGER_NUM, b, sizeof(b))) return false;
  if ((b[0] & 0x0F) == 0) return false;  // no finger down
  x = ((b[1] & 0x0F) << 8) | b[2];
  y = ((b[3] & 0x0F) << 8) | b[4];
  return true;
}
