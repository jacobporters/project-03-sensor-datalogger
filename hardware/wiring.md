# Wiring

## I2C Bus (GPIO 21 SDA, GPIO 22 SCL)
| Device | SDA | SCL | VCC | GND |
|--------|-----|-----|-----|-----|
| INA219 | GPIO 21 | GPIO 22 | 3.3V | GND |
| SSD1306 OLED | GPIO 21 | GPIO 22 | 3.3V | GND |

Both devices share the same I2C bus. INA219 default address: 0x40. SSD1306 address: 0x3C.

## SPI Bus — microSD Module (GPIO 18 SCK, GPIO 19 MISO, GPIO 23 MOSI)
| SD Pin | ESP32 Pin |
|--------|-----------|
| VCC | 5V (VIN) |
| GND | GND |
| MISO | GPIO 19 |
| MOSI | GPIO 23 |
| SCK | GPIO 18 |
| CS | GPIO 15 |

**Important:** The HiLetgo SD module has an AMS1117 3.3V regulator onboard. It must be powered from the ESP32's 5V (VIN) pin, not 3.3V. Powering from 3.3V starves the regulator and causes SD init failures at any SPI clock speed.

SPI clock: 4 MHz (`SD_SCK_MHZ(4)` via SdFat). The Arduino SD library is unreliable on ESP32; SdFat is required.

## 1-Wire — DS18B20 Temperature Sensor
| DS18B20 Leg | Connected to |
|-------------|-------------|
| Left (GND) | GND |
| Middle (DATA) | GPIO 4 |
| Right (VCC) | 3.3V |

Flat face toward you: left = GND, middle = DATA, right = VCC.

**Required:** 4.7kΩ pull-up resistor between DATA (GPIO 4) and 3.3V. The 1-Wire bus will not work without it.

## Button
| Pin | Connected to |
|-----|--------------|
| GPIO 17 | One leg of button |
| GND | Other leg of button |

Configured as `INPUT_PULLUP` in firmware. Active-low: reads LOW when pressed.

**Note:** GPIO 2 must not be used for the button. It is a strapping pin on the ESP32 and is sampled at boot — connecting it to a button causes spurious boot behavior and prevents normal logging operation.
