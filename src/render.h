// Screen layouts for 64x16 and 32x32 panels. Portable.
#pragma once
#include <string>

#include "fb.h"

// Clock digit styles. All fit a 30x14 box so the layout never changes.
enum ClockFont { kFontSegment, kFontRounded, kFontSlim, kFontDots, kClockFontCount };
const char* clockFontName(int font);

struct Screen {
  enum Page { kHome, kEvent, kMessage };
  Page page = kHome;
  bool night = false;

  // Clock
  bool timeValid = false;
  int hh = 0, mm = 0;
  int clockFont = kFontSegment;

  // Weather. Shown with one decimal when |t| < 10 (9.7, -1.3), else whole degrees.
  bool wxValid = false;
  int tempTenths = 0;  // 0.1 degC units
  int icon = 0;        // icons::Id

  // Indoor temperature (NTC), shown with a house icon in the same spot as the
  // weather. Always one decimal when it fits (21.5).
  bool inValid = false;
  int inTenths = 0;

  // Which block sits next to the clock, and how far it has slid away:
  // wxFront = kOutdoor/kIndoor; wxSlide 0..1000 = that block has moved down by
  // this fraction of the block height while the other one comes in from above.
  enum Block { kOutdoor, kIndoor };
  int wxFront = kOutdoor;
  int wxSlide = 0;

  // Home screen hint on 32x32: start time of the next event ("18:30"), may be empty.
  std::string nextLabel;

  // Event page
  std::string evLabel;  // "18:30", "Dnes", "Zajtra 8:00"
  std::string evTitle;

  // Message page
  std::string msg;
  RGB msgColor = {0, 210, 200};
  int msgPage = 0;  // which group of lines to show when the text is long
};

// Draws the screen for the framebuffer's size (64x16 or 32x32).
void renderScreen(const Screen& s, Framebuffer& fb);

// How many pages a message needs on this panel size (>= 1).
int messagePages(const std::string& msg, int panelW, int panelH);

// Temperature text as shown: "9.7", "-1.3", "12", "-15".
std::string tempLabel(int tenths, bool decimal);

// WMO weather code (Open-Meteo) -> icons::Id
int iconForWmo(int code, bool isDay);
