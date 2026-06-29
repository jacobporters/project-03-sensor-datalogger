# Build Notes

## SD Card Init Failures

The Arduino SD library failed silently on ESP32 regardless of wiring. Switching to **SdFat by Bill Greiman** at `SD_SCK_MHZ(4)` resolved it. The standard Arduino SD library's default SPI clock is too aggressive for this module on ESP32.

After switching to SdFat, SD init still failed. Root cause: the HiLetgo SD module has an **AMS1117 3.3V regulator** (`AMS1117 3.3 TKWDE`) and a **74LVC125A level shifter** onboard. The AMS1117 requires 5V input to regulate down to 3.3V. Powering from the ESP32's 3.3V pin gave the regulator insufficient headroom, causing marginal behavior. Moving VCC to the ESP32's **5V (VIN) pin** fixed init immediately.

## GPIO 2 Strapping Pin

Initially used GPIO 2 for the pushbutton. On boot, the ESP32 samples GPIO 2 as a strapping pin to determine flash mode. This caused the logger to start recording without a button press, and subsequent button presses had no effect. Moved button to **GPIO 17**, which is not a strapping pin.

## INA219 Power Register Quantization

Live readings showed power not equal to V × I, and power values were always whole numbers. The INA219 internal power register has **2mW quantization** (1 LSB = 2mW at the 16V/400mA calibration setting) and uses VIN- rather than bus voltage for its calculation. Fixed by computing `powerMW = busV * currentMA` in firmware instead of calling `getPower_mW()`.

## macOS Port 5000 Conflict

Flask defaulted to port 5000, which conflicts with AirPlay Receiver on macOS Ventura and later. Fix: System Settings → General → AirDrop & Handoff → AirPlay Receiver → Off.

## OLED Refresh Rate and Photography

The SSD1306 refresh rate causes banding in phone photos. Added a long-press (2s) on the button to freeze the OLED display, allowing a clean photo. Short press resumes normal operation.
