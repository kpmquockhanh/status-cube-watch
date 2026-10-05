#pragma once
// Pin map for the Waveshare ESP32-S3-Touch-LCD-1.69.
//
// VERIFY THESE AGAINST YOUR BOARD before the first flash -- Waveshare ships
// several 1.69" variants and the revisions do not all share a pinout. The
// schematic and pin table are on the product wiki. Everything board-specific
// in this project lives in this one file, so a different revision is a
// one-file change.

// --- ST7789V2 display, SPI ---------------------------------------------
#define PIN_LCD_DC    4
#define PIN_LCD_CS    5
#define PIN_LCD_SCLK  6
#define PIN_LCD_MOSI  7
#define PIN_LCD_RST   8
#define PIN_LCD_BL   15   // backlight, driven as PWM

#define LCD_WIDTH   240
#define LCD_HEIGHT  280
// The ST7789 controller addresses a 240x320 frame, so a 240x280 panel starts
// 20 rows in. If the image is shifted vertically, this is the number to change.
#define LCD_OFFSET_Y 20
// Corner radius of the glass, in px. The settings sheets round their screen-edge
// corners to this so they sit flush with the bezel; tune it if they do not.
#define LCD_CORNER_R 40

// --- CST816 capacitive touch, I2C (shared with the IMU and RTC) --------
#define PIN_I2C_SDA  11
#define PIN_I2C_SCL  10
#define PIN_TP_INT   14
#define PIN_TP_RST   13
#define CST816_ADDR  0x15

// --- QMI8658C 6-axis IMU, I2C (same bus as the touch controller) --------
#define QMI8658_ADDR 0x6B
// Sign of the IMU's X-axis reading (measured: gravity lies on X with the cube on its edge) when the top of the screen points at the
// ceiling (+1 g = upright). Flip to -1 if the screen rotates the wrong way when
// the cube is stood on its edge; see docs/imu-acceptance.md.
#define IMU_UP_SIGN  1

// --- Misc on-board peripherals -----------------------------------------
#define PIN_BAT_ADC   1   // battery voltage divider
#define PIN_BUZZER   42   // passive buzzer, PWM (buzzer.cpp); an older board revision used 33
