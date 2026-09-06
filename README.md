# ESP8266 BME680 Display v2

A modular rewrite of the recovered ESP8266/BME680/OLED application.

## Target
- ESP8266 / NodeMCU
- BME680 on I2C
- 128x32 SSD1306 OLED
- Adafruit sensor/display libraries
- Arduino ESP8266 core

## Preserved external behaviour
The original HTTP endpoints remain available:
- `/`
- `/sensor/read`
- `/status`
- `/settings`
- `/aplist`
- `/nodemcu.css`
- `/nodemcu.js`
- `/status/json`

The legacy configuration format at EEPROM offset 0/2 is detected and migrated to the v2 configuration format.

## Important changes
- No `exit(1)` from application code.
- No dynamic linked-list allocation for display windows.
- Measurement storage is a real ring buffer with a valid sample count.
- `millis()` scheduling is rollover-safe.
- HTTP requests have timeouts.
- Failed push measurements are buffered in SPIFFS and retried.
- Battery percentage is calculated consistently using 2.78..3.78 V as the legacy intended range.
- The legacy BME680 temperature correction of -4.0 C is retained.
- Gas resistance is named `gasResistance`; the old `VOC` label was a naming mistake.
- Logger variadic formatting is fixed.

## Arduino IDE
Open `esp8266_bme680_display_v2.ino` after copying the `src` files into the sketch folder, or use PlatformIO directly.

## Compatibility note
The old source actually used the extended `esp8266_ap(1).h` configuration, including Domoticz fields and the NodeMCU/FeatherWing button pin mapping. v2 incorporates those settings directly rather than depending on that large generated header.
