// Over-the-air firmware updates, two ways:
//   - Arduino IDE network port ("clock at 192.168.x.x"), via ArduinoOTA
//   - browser upload at http://clock.local/update (or curl, see README)
// Both need a partition layout with two app slots:
// Tools > Partition Scheme > "Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)".
//
// A freshly updated image is only confirmed once it has been on Wi-Fi for 30 s.
// If it crashes before that, or never gets online within 5 min, the bootloader
// goes back to the previous firmware, so a bad build can't lock you out.
#pragma once
#include <Arduino.h>
#include <WebServer.h>

namespace ota {

// Puts a short text on the panel for `seconds`.
using ShowFn = void (*)(const char* text, int seconds);

void begin(WebServer& server, ShowFn show);  // in setup(): /update routes, firmware id
void startNetwork();                         // once Wi-Fi + mDNS are up: IDE network port
void loop(bool wifiUp);                      // every loop: IDE uploads, first-boot health check

String firmwareId();  // first 8 hex digits of the running image's SHA-256
String status();      // one line: id, slot, size, verification state
bool available();     // false when the partition layout has no second app slot

}  // namespace ota
