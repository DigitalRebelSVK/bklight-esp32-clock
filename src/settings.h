// Runtime settings, editable on http://clock.local/ and kept in NVS (flash),
// so they survive a reboot. Defaults come from clock_config.h.
#pragma once
#include <stdint.h>

struct Settings {
  uint8_t clockFont;     // ClockFont (render.h)
  uint8_t brightMode;    // 0 = auto from the light sensor, 1 = manual
  uint8_t dayLevel;      // 0..100: manual level, or the level when there is no sensor
  uint8_t nightLevel;    // 0..100: level in night mode
  uint8_t nightMode;     // 0 = off, 1 = by schedule, 2 = by light sensor
  uint8_t nightStart;    // hour 0..23
  uint8_t nightEnd;      // hour 0..23
  uint8_t nightRed;      // 1 = red clock only, 0 = normal screen, just dimmer
  uint8_t hwBrightness;  // 1 = panel's own brightness command, 0 = dim the pixels
  uint8_t autoMin;       // auto mode maps dark..bright onto autoMin..autoMax
  uint8_t autoMax;
};

void settingsDefaults(Settings& s);
void settingsLoad(Settings& s);
void settingsSave(const Settings& s);

// Sets one field by name with range checking. Returns false for unknown keys
// or out-of-range values (the setting is left unchanged).
bool settingsSet(Settings& s, const char* key, int value);

// Field names, for building the JSON / reading form posts.
extern const char* const kSettingKeys[];
extern const int kSettingCount;
int settingsGet(const Settings& s, const char* key);
