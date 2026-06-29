/*
 * SD card basic init test using SdFat.
 * Used to verify SD module wiring and library before integrating
 * into the main datalogger sketch.
 */

#include <SPI.h>
#include <SdFat.h>

#define SD_CS_PIN 15

SdFat sd;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("SdFat test...");

  if (!sd.begin(SD_CS_PIN, SD_SCK_MHZ(4))) {
    Serial.println("FAILED");
    sd.initErrorHalt();
  } else {
    Serial.println("OK");
    SdFile f;
    if (f.open("/test.txt", O_WRITE | O_CREAT | O_TRUNC)) {
      f.println("hello");
      f.close();
      Serial.println("Write OK");
    } else {
      Serial.println("Write FAILED");
    }
  }
}

void loop() {}
