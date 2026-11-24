// Host-side test + preview generator. Builds the portable core on a PC:
//   g++ -std=c++17 -O2 -Isrc tools/preview.cpp src/render.cpp src/text.cpp
//       src/png.cpp src/calendar.cpp -o preview_tool && ./preview_tool preview
// Writes panel-size PNGs (made by the firmware's own encoder) and BK frames.
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "../src/autobright.h"
#include "../src/calendar.h"
#include "../src/icons.h"
#include "../src/indoor_slide.h"
#include "../src/ntc.h"
#include "../src/png.h"
#include "../src/render.h"
#include "../src/settings.h"
#include "../src/text.h"

static uint8_t gPng[8192];
static uint8_t gFrame[8192 + 16];

static void writeFile(const std::string& path, const uint8_t* d, size_t n) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) { perror(path.c_str()); return; }
  fwrite(d, 1, n, f);
  fclose(f);
}

static void emit(const std::string& dir, const std::string& name, const Framebuffer& fb) {
  size_t n = png::encode(fb.data(), fb.width(), fb.height(), gPng, sizeof(gPng));
  assert(n > 0);
  size_t fn = bkframe::build(gPng, n, 0x65, gFrame, sizeof(gFrame));
  assert(fn == n + 15);
  writeFile(dir + "/raw_" + name + ".png", gPng, n);
  writeFile(dir + "/frame_" + name + ".bin", gFrame, fn);
  writeFile(dir + "/rgb_" + name + ".bin", fb.data(), fb.bytes());
  printf("%-28s png %4zu B  frame %4zu B  (raw %zu B)\n", name.c_str(), n, fn, fb.bytes());
}

static constexpr int INDOOR_SLIDE_MS_PREVIEW = 700;
static constexpr int INDOOR_HOLD_MS_PREVIEW = 5000;

static Screen base(int hh, int mm, int tenths, int wmo, bool day, int font = kFontSlim) {
  Screen s;
  s.timeValid = true;
  s.hh = hh;
  s.mm = mm;
  s.clockFont = font;
  s.wxValid = true;
  s.tempTenths = tenths;
  s.icon = iconForWmo(wmo, day);
  return s;
}

static void scenarios(const std::string& dir, int w, int h) {
  Framebuffer fb(w, h);
  std::string sz = std::to_string(w) + "x" + std::to_string(h);

  // Fonts: same evening screen in every style, plus a morning time for the "1"s
  for (int f = 0; f < kClockFontCount; ++f) {
    std::string fn = clockFontName(f);
    Screen s = base(21, 47, -32, 2, false, f);
    renderScreen(s, fb); emit(dir, sz + "_font_" + fn + "_a", fb);
    s = base(10, 38, 97, 0, true, f);
    renderScreen(s, fb); emit(dir, sz + "_font_" + fn + "_b", fb);
    s = base(2, 41, -5, 0, false, f);
    s.night = true;
    renderScreen(s, fb); emit(dir, sz + "_font_" + fn + "_night", fb);
  }

  // 3-digit times, centred on their own pixels
  {
    Screen s = base(9, 5, 61, 0, true);
    renderScreen(s, fb); emit(dir, sz + "_three_digit_day", fb);
    s = base(21, 5, 61, 0, false);
    s.night = true;
    renderScreen(s, fb); emit(dir, sz + "_four_digit_night", fb);
  }

  // Temperatures: decimals below 10, whole degrees above
  const int temps[] = {97, -13, 0, -97, 104, -153, 255};
  const int codes[] = {0, 73, 3, 71, 61, 75, 95};
  for (int i = 0; i < 7; ++i) {
    Screen s = base(14, 30, temps[i], codes[i], true);
    renderScreen(s, fb); emit(dir, sz + "_temp_" + tempLabel(temps[i], abs(temps[i]) < 100), fb);
  }

  Screen s = base(17, 10, 84, 3, true);
  s.page = Screen::kEvent;
  s.evLabel = "18:30";
  s.evTitle = text::upper("Tréning s Jankom");
  renderScreen(s, fb); emit(dir, sz + "_event_today", fb);

  s = Screen();
  s.page = Screen::kMessage;
  s.msg = text::upper("Práčka dokončila pranie");
  renderScreen(s, fb); emit(dir, sz + "_message", fb);

  s = Screen();  // no NTP / no weather yet
  renderScreen(s, fb); emit(dir, sz + "_home_booting", fb);

  // Indoor temperature, at rest
  s = base(21, 47, -32, 2, false);
  s.inValid = true;
  s.inTenths = 215;
  s.wxFront = Screen::kIndoor;
  renderScreen(s, fb); emit(dir, sz + "_indoor", fb);
  s.inTenths = 268;
  renderScreen(s, fb); emit(dir, sz + "_indoor_warm", fb);

  // End of a slide == the other block at rest (no jump when the slide ends)
  Framebuffer a(w, h), b(w, h);
  for (int front = 0; front < 2; ++front) {
    Screen x = base(21, 47, -32, 2, false);
    x.inValid = true;
    x.inTenths = 215;
    x.wxFront = front;
    x.wxSlide = 1000;
    Screen y = x;
    y.wxFront = 1 - front;
    y.wxSlide = 0;
    renderScreen(x, a);
    renderScreen(y, b);
    assert(memcmp(a.data(), b.data(), a.bytes()) == 0);
  }

  // The minute-change animation, 100 ms apart (the clock sends a frame as soon
  // as the panel ACKs the previous one, so the real rate depends on the panel).
  for (int ms = 0; ms <= 2 * INDOOR_SLIDE_MS_PREVIEW + INDOOR_HOLD_MS_PREVIEW + 300; ms += 100) {
    Screen x = base(21, 48, -32, 2, false);
    x.inValid = true;
    x.inTenths = 215;
    SlideState st = slideAt(ms, true, INDOOR_SLIDE_MS_PREVIEW, INDOOR_HOLD_MS_PREVIEW);
    x.wxFront = st.front;
    x.wxSlide = st.slide;
    char nm[48];
    snprintf(nm, sizeof(nm), "anim%s_%05d", sz.c_str(), ms);
    renderScreen(x, fb); emit(dir, nm, fb);
  }
}

static void testIndoor() {
  // NTC maths: round trip through the divider, -20..60 degC
  for (int c = -20; c <= 60; c += 5) {
    float r = ntc::ohmsAt((float)c, 100000.0f, 3950.0f);
    float mv = ntc::nodeMv(r, 3300.0f, 100000.0f);
    float back = ntc::celsius(ntc::ohms(mv, 3300.0f, 100000.0f), 100000.0f, 3950.0f);
    assert(fabsf(back - c) < 0.01f);
  }
  assert(fabsf(ntc::celsius(100000.0f, 100000.0f, 3950.0f) - 25.0f) < 0.001f);
  assert(fabsf(ntc::nodeMv(100000.0f, 3300.0f, 100000.0f) - 1650.0f) < 0.01f);
  assert(ntc::ohms(3300.0f, 3300.0f, 100000.0f) < 0);  // open circuit
  // Slide timeline
  SlideState st = slideAt(0, true, 700, 5000);
  assert(st.busy && st.front == Screen::kOutdoor && st.slide == 0);
  st = slideAt(350, true, 700, 5000);
  assert(st.slide == 500);
  st = slideAt(700, true, 700, 5000);
  assert(st.front == Screen::kIndoor && st.slide == 0 && st.busy);
  st = slideAt(6050, true, 700, 5000);
  assert(st.front == Screen::kIndoor && st.slide == 500);
  st = slideAt(6400, true, 700, 5000);
  assert(!st.busy && st.front == Screen::kOutdoor && st.slide == 0);
  st = slideAt(300, false, 700, 5000);
  assert(!st.busy && st.slide == 0);
  printf("indoor ok\n");
}

static void testCalendar() {
  Calendar cal;
  int n = cal.setFromText(
      "2026-11-13 18:30 Tréning s Jankom\r\n"
      "2026-11-13 * Babka Žofia má meniny\n"
      "2026-11-13 07:00 Already over\n"
      "2026-11-14 08:00 Zubár\n"
      "garbage line\n"
      "2026-11-15 10:00 Too far ahead\n");
  assert(n == 5);
  Today now{2026, 11, 13, 17 * 60 + 10};
  auto up = cal.upcoming(now, 15, 18);
  assert(up.size() == 2);  // all-day + 18:30 (tomorrow hidden before 18:00)
  assert(up[0].minute == -1);
  assert(up[1].minute == 18 * 60 + 30);
  assert(Calendar::label(up[1], now, "Dnes", "Zajtra") == "18:30");
  Today evening{2026, 11, 13, 19 * 60};
  up = cal.upcoming(evening, 15, 18);
  assert(up.size() == 2);  // all-day today + tomorrow 8:00 (18:30 is > 15 min ago)
  assert(Calendar::label(up[1], evening, "Dnes", "Zajtra") == "Zajtra 8:00");
  // month rollover
  Calendar c2;
  c2.setFromText("2026-12-01 09:00 Rollover");
  Today nov30{2026, 11, 30, 20 * 60};
  assert(c2.upcoming(nov30, 15, 18).size() == 1);
  printf("calendar tests ok\n");
}

static void testWrap() {
  auto l = text::wrap("Tréning s Jankom", 7, 13, 2);
  assert(l.size() == 2 && l[0] == "Tréning" && l[1] == "s Jankom");
  l = text::wrap("Veľmi dlhý názov udalosti ktorý sa nezmestí", 13, 13, 2);
  assert(l.size() == 2);
  assert(text::length(l[1]) <= 13);
  assert(l[1].find("\xE2\x80\xA6") != std::string::npos);  // ellipsis
  l = text::wrap("Supercalifragilistic", 6, 6, 0);
  assert(l.size() == 4 && l[0] == "Super-");
  printf("wrap tests ok\n");
}

static void testAutoBright() {
  AutoBright ab;
  for (int i = 0; i < 30; ++i) ab.update(0.3f, 1.0f);  // dark room
  assert(ab.night());
  assert(ab.level() == ab.config().minLevel);
  int steps = 0;
  while (ab.level() < 80 && steps < 60) { ab.update(300.0f, 1.0f); ++steps; }
  printf("autobright: dark->300 lx reaches level %d in %d s, night=%d\n", ab.level(), steps, ab.night());
  assert(!ab.night());
  for (int i = 0; i < 60; ++i) ab.update(300.0f, 1.0f);  // settle
  // one-second shadow at 300 lx should not move the level by a full step
  uint8_t before = ab.level();
  ab.update(30.0f, 1.0f);
  ab.update(300.0f, 1.0f);
  printf("autobright: passing shadow moved level %d -> %d\n", before, ab.level());
}

int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : "preview";
  testCalendar();
  testWrap();
  testAutoBright();
  testIndoor();
  scenarios(dir, 64, 16);
  scenarios(dir, 32, 32);
  assert(tempLabel(97, true) == "9.7" && tempLabel(-13, true) == "-1.3");
  assert(tempLabel(-4, true) == "-0.4" && tempLabel(0, true) == "0.0");
  assert(tempLabel(104, false) == "10" && tempLabel(-155, false) == "-16" && tempLabel(-154, false) == "-15");
  printf("temp label ok\n");
  Settings cfg;
  settingsDefaults(cfg);
  assert(settingsSet(cfg, "dayLevel", 70) && cfg.dayLevel == 70);
  assert(!settingsSet(cfg, "dayLevel", 101) && cfg.dayLevel == 70);
  assert(!settingsSet(cfg, "clockFont", kClockFontCount));
  assert(settingsSet(cfg, "clockFont", kFontSlim) && settingsGet(cfg, "clockFont") == kFontSlim);
  assert(!settingsSet(cfg, "nope", 1));
  assert(cfg.indoorSlide == 1 && cfg.indoorOffset == 50);
  assert(settingsSet(cfg, "indoorOffset", 42) && settingsGet(cfg, "indoorOffset") == 42);
  assert(!settingsSet(cfg, "indoorOffset", 101));
  for (int i = 0; i < kSettingCount; ++i) assert(settingsGet(cfg, kSettingKeys[i]) >= 0);
  printf("settings ok\n");
  assert(text::upper("žofia ľubka čšťďňôáéíúý") == "ŽOFIA ĽUBKA ČŠŤĎŇÔÁÉÍÚÝ");
  printf("upper ok\n");
  return 0;
}
