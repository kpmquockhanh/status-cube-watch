#include "imu.h"
#include <Arduino.h>
#include <Wire.h>
#include "board_pins.h"

namespace {
constexpr uint8_t REG_WHO_AM_I = 0x00;  // reads 0x05
constexpr uint8_t REG_CTRL1 = 0x02;     // bit 6: register address auto-increment
constexpr uint8_t REG_CTRL2 = 0x03;     // accelerometer: full scale [6:4], output rate [3:0]
constexpr uint8_t REG_CTRL7 = 0x08;     // bit 0: accelerometer enable, bit 1: gyro enable
constexpr uint8_t REG_ACC_X_L = 0x35;   // x, y, z as little-endian int16
constexpr uint8_t REG_RESET = 0x60;
constexpr uint8_t WHO_AM_I_VALUE = 0x05;
constexpr uint8_t RESET_CMD = 0xB0;
constexpr uint8_t CTRL1_ADDR_AUTO_INC = 0x40;
constexpr uint8_t CTRL2_4G_62HZ = 0x17;  // +-4 g, 62.5 Hz: plenty for a 5 Hz orientation poll
constexpr uint8_t CTRL7_ACC_ONLY = 0x01;
constexpr float LSB_PER_G = 8192.0f;     // +-4 g full scale

bool g_ready = false;

bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(QMI8658_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission(true) == 0;
}

bool readRegs(uint8_t reg, uint8_t *buf, size_t len) {
  Wire.beginTransmission(QMI8658_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom((int)QMI8658_ADDR, (int)len) != (int)len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}
}  // namespace

bool imuBegin() {
  g_ready = false;
  uint8_t id = 0;
  if (!readRegs(REG_WHO_AM_I, &id, 1)) {
    Serial.println("[imu] not responding on I2C -- auto-rotate off");
    return false;
  }
  Serial.printf("[imu] QMI8658 who-am-i 0x%02X\n", id);
  if (id != WHO_AM_I_VALUE) {
    Serial.println("[imu] unexpected chip id (wanted 0x05) -- auto-rotate off");
    return false;
  }
  writeReg(REG_RESET, RESET_CMD);
  delay(15);  // the reset takes a few ms to finish
  g_ready = writeReg(REG_CTRL1, CTRL1_ADDR_AUTO_INC) && writeReg(REG_CTRL2, CTRL2_4G_62HZ) &&
            writeReg(REG_CTRL7, CTRL7_ACC_ONLY);
  if (!g_ready) Serial.println("[imu] configuration failed -- auto-rotate off");
  return g_ready;
}

bool imuReadAccel(float &ax, float &ay, float &az) {
  if (!g_ready) return false;
  uint8_t b[6];
  if (!readRegs(REG_ACC_X_L, b, sizeof(b))) return false;
  ax = (int16_t)(b[0] | (b[1] << 8)) / LSB_PER_G;
  ay = (int16_t)(b[2] | (b[3] << 8)) / LSB_PER_G;
  az = (int16_t)(b[4] | (b[5] << 8)) / LSB_PER_G;
  return true;
}
