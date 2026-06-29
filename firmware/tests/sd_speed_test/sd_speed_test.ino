/*
 * SD card SPI clock speed test.
 * Iterates through 1, 2, 4, 8 MHz to find the highest
 * reliable clock speed for a given SD module.
 *
 * Root cause of SD init failures on cheap modules:
 * the AMS1117 3.3V regulator on the HiLetgo module requires
 * 5V input. Powering from ESP32 3.3V starves the regulator
 * and causes marginal behavior at higher clock speeds.
 * Fix: power SD module VCC from ESP32 5V (VIN) pin.
 */

#include <SPI.h>
#include <SdFat.h>

#define SD_CS_PIN 15

SdFat sd;

void setup() {
  Serial.begin(115200);
  delay(1000);

  int speeds[] = {1, 2, 4, 8};
  for (int i = 0; i < 4; i++) {
    Serial.print("Trying ");
    Serial.print(speeds[i]);
    Serial.print(" MHz... ");
    if (sd.begin(SD_CS_PIN, SD_SCK_MHZ(speeds[i]))) {
      Serial.println("OK");
      break;
    } else {
      Serial.println("FAILED");
      delay(500);
    }
  }
}

void loop() {}
