# ESP32-S3-Touch-LCD-1.69

URL: https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.69

[![SP32-S3-Touch-LCD-1.69](https://docs.waveshare.com/assets/images/ESP32-S3-Touch-LCD-1.69-details-1-37f4207b24628a107d256c4bee148a81.webp)](https://www.waveshare.com/esp32-s3-touch-lcd-1.69.htm)

The ESP32-S3-Touch-LCD-1.69 is a low-cost, high-performance microcontroller development board designed by Waveshare. Powered by the **ESP32-S3R8** dual-core LX7 processor (240 MHz) with 2.4 GHz Wi-Fi and Bluetooth 5 (LE), featuring 512KB SRAM, 8MB stacked PSRAM and 16MB Flash. Its compact form factor integrates a 1.69inch capacitive touch LCD (240×280), Li-battery charging, a 6-axis IMU, RTC, and other peripherals, making it convenient for rapid prototyping and embedded applications.

| SKU | Product |
| --- | --- |
| 27350 | ESP32-S3-Touch-LCD-1.69 |

**If you are looking for:**

- **Schematics, datasheets, and example code** : See [Resources](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.69/Resources-And-Documents)
- **Arduino development** : See [Working with Arduino](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.69/Arduino)
- **ESP-IDF development** : See [Working with ESP-IDF](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.69/ESP-IDF)
- **Troubleshooting** : See [Product FAQ](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.69/FAQ) or contact [Technical Support](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.69/Technical-Support)

## Onboard Resources

![ESP32-S3-Touch-LCD-1.69 Hardware Resources 1](https://docs.waveshare.com/assets/images/ESP32-S3-Touch-LCD-1.69-HW-Intro1-c76d4e958a92a29a9ca1d5f99bb4ccd5.webp)

1. **ESP32-S3R8** Wi-Fi and Bluetooth SoC, 240 MHz, 8MB stacked PSRAM
2. **W25Q128JVSIQ** 16MB NOR Flash
3. **PCF85063** RTC clock chip
4. **QMI8658** 6-axis IMU (3-axis gyroscope + 3-axis accelerometer)
5. **ETA6098** high-efficiency Li-battery charging chip
6. **MX1.25 Li-battery connector** MX1.25 2P connector for 3.7V Li-battery, supports charging/discharging
7. **Type-C port** ESP32-S3 USB, for program uploading and log printing
8. **Buzzer** audible peripheral
9. **RTC battery connector** for rechargeable RTC battery, supports charging/discharging
10. **Onboard chip antenna** supports 2.4 GHz Wi-Fi (802.11 b/g/n) and Bluetooth® 5 (LE)
11. **Power function button** supports power-on detection, single-click, double-click, multi-click and long-press
12. **BOOT button**
13. **RST reset button**

### Peripheral Quick Reference

| Module | Device / Function | Interface | Address / Parameters | GPIO / Signal |
| --- | --- | --- | --- | --- |
| LCD | ST7789V2 | 4-wire SPI | 240×280, RGB565, MISO unused | DC=GPIO4, CS=GPIO5, CLK=GPIO6, DIN=GPIO7, RST=GPIO8, BL=GPIO15 |
| Touch | CST816T capacitive touch | I2C | 7-bit address `0x15` | SCL=GPIO10, SDA=GPIO11, RST=GPIO13, INT=GPIO14 |
| IMU | QMI8658C 6-axis sensor | I2C | 7-bit address `0x6B` | SCL=GPIO10, SDA=GPIO11, INT1=GPIO38 |
| RTC | PCF85063ATL | I2C | 7-bit address `0x51` , 32.768 kHz crystal | SCL=GPIO10, SDA=GPIO11, INT=GPIO39 |
| Buzzer | Onboard buzzer | GPIO / PWM | - | GPIO42 |
| Battery sampling | B+ voltage divider to ADC | ADC | R3 pull-up 200K, R7 pull-down 100K; VBAT=VADC×3 | GPIO1 / BAT_ADC |
| Power control | SYS_OUT / SYS_EN | GPIO | PWR / Key2 power circuit | SYS_OUT=GPIO40, SYS_EN=GPIO41 |
| USB Type-C | ESP32-S3 native USB | USB | Upload, logging | USB_N=GPIO19, USB_P=GPIO20 |
| UART0 | Default serial | UART | Debug / expansion pads | U0TXD=GPIO43, U0RXD=GPIO44 |
| Charge management | ETA6098 | Power | Single-cell Li-battery | PH1.25 / M1.25 battery connector |
| 3.3V LDO | ME6217C33M5G | Power | System 3.3V | VCC3V3 |
| PWR button | Key2 / Power | GPIO / power control | SYS_OUT pulled up, pulled low when pressed; SYS_EN controls battery power hold | SYS_OUT=GPIO40, SYS_EN=GPIO41 |
| BOOT button | Key1 / BOOT | GPIO / download mode | Pulled up to 3.3V, pulled low when pressed | GPIO0 |
| RST button | Key3 / Reset | Reset | Pulled up to 3.3V, pulled low when pressed | CHIP_PU |

## Pinout Definition

When using the GPIO pins reserved on the ESP32-S3-Touch-LCD-1.69 board, pay attention to the wire colors and corresponding functions to avoid burnout of the development board due to wiring habits.

![ESP32-S3-Touch-LCD-1.69 Pin Definition](https://docs.waveshare.com/assets/images/ESP32-S3-Touch-LCD-1.69-Pinout-9eb7123eddd52dc51577bba392a34e65.webp)

### Extension Port

| Type | Signal |
| --- | --- |
| Power | `5V` / `3V3` / `GND` |
| I2C | `SCL(GPIO10)` / `SDA(GPIO11)` |
| UART | `TX(GPIO43)` / `RX(GPIO44)` |
| GPIO | `GPIO2` / `GPIO3` / `GPIO17` / `GPIO18` |

### Complete GPIO Allocation

| GPIO | Signal Name | Connected To | Notes |
| --- | --- | --- | --- |
| GPIO0 | BOOT / Key1 | BOOT button | Strapping pin; hold while powering on, then release to enter download mode |
| GPIO1 | BAT_ADC | Battery voltage divider | R3 pull-up 200K, R7 pull-down 100K; VBAT=VADC×3 |
| GPIO2 | GPIO2 | Reserved pad / header | Extension port GPIO2 |
| GPIO3 | GPIO3 | Reserved pad / header | Extension port GPIO3 |
| GPIO4 | LCD_DC | ST7789V2 data/command | - |
| GPIO5 | LCD_CS | ST7789V2 chip select | - |
| GPIO6 | LCD_CLK | ST7789V2 SPI clock | - |
| GPIO7 | LCD_DIN | ST7789V2 SPI data | Only connected to the LCD SPI write data line, LCD_DOUT unused |
| GPIO8 | LCD_RST | ST7789V2 reset | - |
| GPIO10 | ESP32_SCL | Touch + IMU + RTC shared I2C SCL; also exposed on extension port | See peripheral table for I2C devices |
| GPIO11 | ESP32_SDA | Touch + IMU + RTC shared I2C SDA; also exposed on extension port | See peripheral table for I2C devices |
| GPIO13 | TP_RST | CST816T touch reset | - |
| GPIO14 | TP_INT | CST816T touch interrupt | - |
| GPIO15 | LCD_BL | LCD backlight control | - |
| GPIO17 | GPIO17 | Reserved pad / header | Extension port GPIO17 |
| GPIO18 | GPIO18 | Reserved pad / header | Extension port GPIO18 |
| GPIO19 | USB_N | USB Type-C D- | ESP32-S3 native USB |
| GPIO20 | USB_P | USB Type-C D+ | ESP32-S3 native USB |
| GPIO38 | QMI_INT2 | QMI8658C INT2 | - |
| GPIO39 | RTC_INT | PCF85063 RTC interrupt | - |
| GPIO40 | SYS_OUT | System power control network | Related to PWR / Key2 power circuit |
| GPIO41 | SYS_EN | System power control network | Related to PWR / Key2 power circuit |
| GPIO42 | Buzz | Onboard buzzer driver | Can be used as PWM / tone output |
| GPIO43 | U0TXD | Reserved UART TX pad / header | Extension port TX |
| GPIO44 | U0RXD | Reserved UART RX pad / header | Extension port RX |

LCD and Touch Pins
| Signal | GPIO | Description |
| --- | --- | --- |
| `LCD_DC` | `GPIO4` | ST7789V2 data/command |
| `LCD_CS` | `GPIO5` | ST7789V2 chip select |
| `LCD_CLK` | `GPIO6` | ST7789V2 SPI clock |
| `LCD_DIN` | `GPIO7` | ST7789V2 SPI data (write only; DOUT is unused) |
| `LCD_RST` | `GPIO8` | ST7789V2 reset |
| `LCD_BL` | `GPIO15` | LCD backlight control |
| `TP_SCL` | `GPIO10` | Touch I2C SCL (shared) |
| `TP_SDA` | `GPIO11` | Touch I2C SDA (shared) |
| `TP_RST` | `GPIO13` | CST816T touch reset |
| `TP_INT` | `GPIO14` | CST816T touch interrupt |

Power and Buttons
| Signal / Button | GPIO / Signal | Description |
| --- | --- | --- |
| `BOOT` / Key1 | `GPIO0` | Download mode strapping pin; hold while powering on, then release |
| `BAT_ADC` | `GPIO1` | Battery voltage divider; `VBAT=VADC×3` |
| `SYS_OUT` | `GPIO40` | System power control network (PWR / Key2 circuit) |
| `SYS_EN` | `GPIO41` | System power hold network (PWR / Key2 circuit) |
| `Buzz` | `GPIO42` | Onboard buzzer; supports PWM / tone output |
| `RST` / Key3 | `CHIP_PU` | Reset signal |

I2C Device Summary
| Device | Model | I2C Address | I2C Pins | Additional Interrupt / Control |
| --- | --- | --- | --- | --- |
| Touch | CST816T | `0x15` | `GPIO10` / `GPIO11` | `TP_INT=GPIO14` , `TP_RST=GPIO13` |
| IMU | QMI8658C | `0x6B` | `GPIO10` / `GPIO11` | `QMI_INT1=GPIO38` |
| RTC | PCF85063ATL | `0x51` | `GPIO10` / `GPIO11` | `RTC_INT=GPIO39` |

### Usage Notes

- When connecting external I2C devices, avoid address conflicts with `0x15` , `0x6B` , and `0x51` .
- `GPIO19/GPIO20` are connected to USB Type-C and are not recommended for general GPIO use.
- `GPIO0` is the BOOT pin and `CHIP_PU` is the reset signal; not recommended as general user inputs.

## Dimensions

![ESP32-S3-Touch-LCD-1.69 Product Dimensions](https://docs.waveshare.com/assets/images/ESP32-S3-Touch-LCD-1.69-Size-fe23433152821925ce45df66b3caa8c7.webp)

## Development Methods

The ESP32-S3-Touch-LCD-1.69 supports two development frameworks: Arduino IDE and ESP-IDF, providing flexibility for developers to choose the tool that best fits their project requirements and personal preference.

Arduino generally offers a gentler learning curve and may be more approachable for beginners and hobbyists. ESP-IDF provides advanced tooling and finer control over system behavior, making it better suited to complex projects and applications with demanding performance requirements.

- **Arduino IDE** is a convenient, flexible, and easy-to-use open-source electronics prototyping platform. It requires minimal foundational knowledge, allowing for rapid development after a short learning period. Arduino has a huge global user community, providing a vast amount of open-source code, project examples, and tutorials, as well as a rich library ecosystem that encapsulates complex functions, enabling developers to implement various features rapidly. You can refer to the **[Working with Arduino](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.69/Arduino)** to complete the initial setup, and the tutorial also provides related examples for reference.
- **ESP-IDF** , short for Espressif IoT Development Framework, is a professional development framework launched by Espressif Systems for its ESP series of chips. It is based on C language development and includes compilers, debuggers, flashing tools, etc. It supports development via command line or integrated development environments (such as Visual Studio Code with the Espressif IDF plugin), which provides features like code navigation, project management, and debugging. We recommend using VS Code for development. For the specific configuration process, please refer to the **[Working with ESP-IDF](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.69/ESP-IDF)** . The tutorial also provides relevant examples for reference.

[Give Feedback](https://docs.google.com/forms/d/e/1FAIpQLSfayJEZ5J-dp-3Wq_dkWsVRhNuQ6C79_GNV62mYuFW8Mj6U8Q/viewform?entry.803599728=https%3A%2F%2Fdocs.waveshare.com%2FESP32-S3-Touch-LCD-1.69)