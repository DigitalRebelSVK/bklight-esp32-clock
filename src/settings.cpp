#include "settings.h"

#include <string.h>

#include "config_defaults.h"
#include "render.h"

#ifdef ARDUINO
#include <Preferences.h>
#endif


namespace {
constexpr uint8_t kVersion = 1;

struct Field {
  const char* key;
  uint8_t Settings::*ptr;
  int min, max;
};

const Field kFields[] = {
    {"clockFont", &Settings::clockFont, 0, kClockFontCount - 1},
    {"brightMode", &Settings::brightMode, 0, 1},
    {"dayLevel", &Settings::dayLevel, 0, 100},
    {"nightLevel", &Settings::nightLevel, 0, 100},
    {"nightMode", &Settings::nightMode, 0, 2},
    {"nightStart", &Settings::nightStart, 0, 23},
    {"nightEnd", &Settings::nightEnd, 0, 23},
    {"nightRed", &Settings::nightRed, 0, 1},
    {"hwBrightness", &Settings::hwBrightness, 0, 1},
    {"autoMin", &Settings::autoMin, 0, 100},
    {"autoMax", &Settings::autoMax, 0, 100},
    {"indoorSlide", &Settings::indoorSlide, 0, 1},
    {"indoorOffset", &Settings::indoorOffset, 0, 100},
};
constexpr int kFieldCount = sizeof(kFields) / sizeof(kFields[0]);

const Field* find(const char* key) {
  for (const Field& f : kFields)
    if (strcmp(f.key, key) == 0) return &f;
  return nullptr;
}
}  // namespace

const char* const kSettingKeys[] = {"clockFont", "brightMode", "dayLevel",  "nightLevel",
                                    "nightMode", "nightStart", "nightEnd",  "nightRed",
                                    "hwBrightness", "autoMin", "autoMax", "indoorSlide",
                                    "indoorOffset"};
const int kSettingCount = kFieldCount;

void settingsDefaults(Settings& s) {
  s.clockFont = CLOCK_FONT;
  s.brightMode = 0;
  s.dayLevel = BRIGHT_FIXED;
  s.nightLevel = BRIGHT_MIN;
  s.nightMode = 1;
  s.nightStart = NIGHT_START_H;
  s.nightEnd = NIGHT_END_H;
  s.nightRed = 1;
  s.hwBrightness = USE_HW_BRIGHTNESS;
  s.autoMin = BRIGHT_MIN;
  s.autoMax = BRIGHT_MAX;
  s.indoorSlide = 1;
  s.indoorOffset = 50;
}

bool settingsSet(Settings& s, const char* key, int value) {
  const Field* f = find(key);
  if (!f || value < f->min || value > f->max) return false;
  s.*(f->ptr) = (uint8_t)value;
  return true;
}

int settingsGet(const Settings& s, const char* key) {
  const Field* f = find(key);
  return f ? s.*(f->ptr) : -1;
}

#ifdef ARDUINO
void settingsLoad(Settings& s) {
  settingsDefaults(s);
  Preferences p;
  if (!p.begin("lrclock", true)) return;
  size_t n = p.getBytesLength("s");
  // A record saved by an older firmware is shorter: take what it has, the
  // fields added since then keep their defaults.
  if (p.getUChar("ver", 0) == kVersion && n > 0 && n <= sizeof(Settings)) {
    Settings stored = s;
    p.getBytes("s", &stored, n);
    // Re-validate every field so a corrupted blob can't break the clock.
    bool ok = true;
    for (const Field& f : kFields) {
      int v = stored.*(f.ptr);
      if (v < f.min || v > f.max) ok = false;
    }
    if (ok) s = stored;
  }
  p.end();
}

void settingsSave(const Settings& s) {
  Preferences p;
  if (!p.begin("lrclock", false)) return;
  p.putUChar("ver", kVersion);
  p.putBytes("s", &s, sizeof(s));
  p.end();
}
#else
void settingsLoad(Settings& s) { settingsDefaults(s); }
void settingsSave(const Settings&) {}
#endif
