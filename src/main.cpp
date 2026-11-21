// Compile the root sketch as C++ in PlatformIO: its embedded JavaScript
// functions confuse automatic Arduino prototype generation.
// Arduino IDE builds the sketch itself, so do not include it there.
#ifdef PLATFORMIO
// Make this framework dependency visible to PlatformIO's library finder.
#include <ESP8266WebServer.h>
#include "../esp8266_bme680_display.ino"
#endif
