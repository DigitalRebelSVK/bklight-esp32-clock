#include "render.h"

#include <stdio.h>
#include <stdlib.h>

#include "icons.h"
#include "text.h"

namespace {

// ---- palette ---------------------------------------------------------------
const RGB kClock = {255, 196, 130};      // warm white
const RGB kClockNight = {170, 14, 0};    // deep red, easy on dark-adapted eyes
const RGB kDim = {70, 70, 70};
const RGB kAccent = {255, 150, 0};       // event times
const RGB kText = {225, 225, 225};

RGB tempColor(int tenths) {
  if (tenths <= 0) return {110, 175, 255};
  if (tenths >= 250) return {255, 135, 40};
  return {230, 230, 230};
}

RGB iconColor(char c) {
  switch (c) {
    case 'Y': return {255, 185, 0};
    case 'M': return {225, 225, 170};
    case 'W': return {205, 205, 215};
    case 'G': return {125, 125, 140};
    case 'D': return {80, 80, 100};
    case 'B': return {40, 120, 255};
    case 'S': return {215, 235, 255};
    case 'L': return {255, 230, 0};
    case 'F': return {150, 150, 150};
    case 'R': return {215, 75, 45};
    case 'H': return {215, 200, 175};
    default: return {0, 0, 0};
  }
}

void drawIcon(Framebuffer& fb, int x, int y, int id) {
  if (id < 0 || id >= icons::kCount) return;
  for (int r = 0; r < icons::kSize; ++r) {
    const char* row = icons::kArt[id][r];
    for (int c = 0; c < icons::kSize && row[c]; ++c)
      if (row[c] != '.') fb.set(x + c, y + r, iconColor(row[c]));
  }
}

// ---- clock fonts ---------------------------------------------------------------
//  7-segment bits: a top, b upper right, c lower right, d bottom, e lower left,
//                  f upper left, g middle
const uint8_t kSeg[10] = {
    0b0111111, 0b0000110, 0b1011011, 0b1001111, 0b1100110,
    0b1101101, 0b1111101, 0b0000111, 0b1111111, 0b1101111,
};
constexpr uint8_t kSegDash = 0b1000000;

// Blocky: segments meet in solid corners.
void segBlocky(Framebuffer& fb, int x, int y, int w, int h, int t, uint8_t seg, RGB c) {
  int mid = y + (h - t) / 2;
  if (seg & 0x01) fb.fillRect(x, y, w, t, c);
  if (seg & 0x02) fb.fillRect(x + w - t, y, t, mid - y + t, c);
  if (seg & 0x04) fb.fillRect(x + w - t, mid, t, y + h - mid, c);
  if (seg & 0x08) fb.fillRect(x, y + h - t, w, t, c);
  if (seg & 0x10) fb.fillRect(x, mid, t, y + h - mid, c);
  if (seg & 0x20) fb.fillRect(x, y, t, mid - y + t, c);
  if (seg & 0x40) fb.fillRect(x, mid, w, t, c);
}

// Rounded: horizontals are 1 px shorter on each side and verticals start 1 px
// lower, so every outer corner pixel stays dark -> soft LED-clock look.
void segRounded(Framebuffer& fb, int x, int y, int w, int h, int t, uint8_t seg, RGB c) {
  int mid = y + (h - t) / 2;
  if (seg & 0x01) fb.fillRect(x + 1, y, w - 2, t, c);
  if (seg & 0x02) fb.fillRect(x + w - t, y + 1, t, mid - y + t - 1, c);
  if (seg & 0x04) fb.fillRect(x + w - t, mid, t, y + h - 1 - mid, c);
  if (seg & 0x08) fb.fillRect(x + 1, y + h - t, w - 2, t, c);
  if (seg & 0x10) fb.fillRect(x, mid, t, y + h - 1 - mid, c);
  if (seg & 0x20) fb.fillRect(x, y + 1, t, mid - y + t - 1, c);
  if (seg & 0x40) fb.fillRect(x + 1, mid, w - 2, t, c);
}

// Slim: hand-drawn 1 px strokes, 6x14. Less glare at night, more "watch face".
const char* const kSlim[10][14] = {
    {".####.", "#....#", "#....#", "#....#", "#....#", "#....#", "#....#",
     "#....#", "#....#", "#....#", "#....#", "#....#", "#....#", ".####."},
    {"...#..", "..##..", ".#.#..", "...#..", "...#..", "...#..", "...#..",
     "...#..", "...#..", "...#..", "...#..", "...#..", "...#..", ".#####"},
    {".####.", "#....#", ".....#", ".....#", ".....#", ".....#", "....#.",
     "...#..", "..#...", ".#....", "#.....", "#.....", "#.....", "######"},
    {".####.", "#....#", ".....#", ".....#", ".....#", ".....#", "..###.",
     ".....#", ".....#", ".....#", ".....#", ".....#", "#....#", ".####."},
    {"....#.", "...##.", "...##.", "..#.#.", "..#.#.", ".#..#.", ".#..#.",
     "#...#.", "######", "....#.", "....#.", "....#.", "....#.", "....#."},
    {"######", "#.....", "#.....", "#.....", "#.....", "#####.", ".....#",
     ".....#", ".....#", ".....#", ".....#", ".....#", "#....#", ".####."},
    {"..###.", ".#....", "#.....", "#.....", "#.....", "#.###.", "##...#",
     "#....#", "#....#", "#....#", "#....#", "#....#", "#....#", ".####."},
    {"######", ".....#", ".....#", "....#.", "....#.", "....#.", "...#..",
     "...#..", "...#..", "..#...", "..#...", "..#...", "..#...", "..#..."},
    {".####.", "#....#", "#....#", "#....#", "#....#", "#....#", ".####.",
     "#....#", "#....#", "#....#", "#....#", "#....#", "#....#", ".####."},
    {".####.", "#....#", "#....#", "#....#", "#....#", "#....#", "#...##",
     ".###.#", ".....#", ".....#", ".....#", ".....#", "....#.", ".###.."},
};

// Dots: a 3x7 digit font drawn on a 2 px dot pitch (5x13), like a stadium
// scoreboard or a railway departure board.
const char* const kDots[10][7] = {
    {"###", "#.#", "#.#", "#.#", "#.#", "#.#", "###"},
    {".#.", "##.", ".#.", ".#.", ".#.", ".#.", "###"},
    {"###", "..#", "..#", "###", "#..", "#..", "###"},
    {"###", "..#", "..#", "###", "..#", "..#", "###"},
    {"#.#", "#.#", "#.#", "###", "..#", "..#", "..#"},
    {"###", "#..", "#..", "###", "..#", "..#", "###"},
    {"###", "#..", "#..", "###", "#.#", "#.#", "###"},
    {"###", "..#", "..#", ".#.", ".#.", ".#.", ".#."},
    {"###", "#.#", "#.#", "###", "#.#", "#.#", "###"},
    {"###", "#.#", "#.#", "###", "..#", "..#", "###"},
};

struct FontMetrics {
  int w, h;    // digit box
  int t;       // stroke (segment fonts) / colon dot size
  int colonW;  // colon column width
};

FontMetrics metrics(int font, bool square) {
  switch (font) {
    case kFontSlim: return {6, 14, 2, 2};
    case kFontDots: return {5, 13, 1, 1};
    case kFontRounded:
    case kFontSegment:
    default: return {6, square ? 12 : 14, 2, 2};
  }
}

int clockWidth(const FontMetrics& m) { return 4 * m.w + 4 + m.colonW; }

// digit < 0 = blank, 10 = dash (no time yet)
void drawDigit(Framebuffer& fb, int font, const FontMetrics& m, int x, int y, int digit, RGB c) {
  if (digit < 0) return;
  switch (font) {
    case kFontSlim:
      if (digit == 10) {
        fb.fillRect(x + 1, y + m.h / 2, m.w - 2, 1, c);
        return;
      }
      for (int r = 0; r < 14; ++r)
        for (int col = 0; col < 6; ++col)
          if (kSlim[digit][r][col] == '#') fb.set(x + col, y + r, c);
      return;
    case kFontDots:
      if (digit == 10) {
        for (int col = 0; col < 3; ++col) fb.set(x + col * 2, y + 6, c);
        return;
      }
      for (int r = 0; r < 7; ++r)
        for (int col = 0; col < 3; ++col)
          if (kDots[digit][r][col] == '#') fb.set(x + col * 2, y + r * 2, c);
      return;
    case kFontRounded:
      segRounded(fb, x, y, m.w, m.h, m.t, digit == 10 ? kSegDash : kSeg[digit], c);
      return;
    default:
      segBlocky(fb, x, y, m.w, m.h, m.t, digit == 10 ? kSegDash : kSeg[digit], c);
      return;
  }
}

void drawColon(Framebuffer& fb, int font, const FontMetrics& m, int x, int y, RGB c) {
  if (font == kFontDots) {  // stay on the dot grid
    fb.set(x, y + 4, c);
    fb.set(x, y + 8, c);
    return;
  }
  int mid = y + (m.h - m.t) / 2;
  fb.fillRect(x, mid - m.t - 1, m.colonW, m.t, c);
  fb.fillRect(x, mid + m.t + 1, m.colonW, m.t, c);
}

bool hasLeadingDigit(bool valid, int hh) { return !valid || hh >= 10; }  // "--:--" keeps 4 slots

// Width of what is actually drawn: H:MM has no blank leading slot, so a
// 3-digit time centres on its own pixels instead of on the colon.
int clockVisibleWidth(const FontMetrics& m, bool valid, int hh) {
  return clockWidth(m) - (hasLeadingDigit(valid, hh) ? 0 : m.w + 1);
}

// Draws H:MM / HH:MM starting at x (the first lit column). Returns the first
// free column after the last digit.
int drawClock(Framebuffer& fb, int font, bool square, int x, int y, bool valid, int hh, int mm,
              RGB color) {
  FontMetrics m = metrics(font, square);
  RGB c = valid ? color : kDim;
  int d[4] = {10, 10, 10, 10};
  if (valid) {
    d[0] = hh / 10;
    d[1] = hh % 10;
    d[2] = mm / 10;
    d[3] = mm % 10;
  }
  int cx = x;
  if (hasLeadingDigit(valid, hh)) {
    drawDigit(fb, font, m, cx, y, d[0], c);
    cx += m.w + 1;
  }
  drawDigit(fb, font, m, cx, y, d[1], c);
  cx += m.w + 1;
  drawColon(fb, font, m, cx, y, c);
  cx += m.colonW + 1;
  drawDigit(fb, font, m, cx, y, d[2], c);
  cx += m.w + 1;
  drawDigit(fb, font, m, cx, y, d[3], c);
  return cx + m.w;
}

// ---- temperature -----------------------------------------------------------------
// Degree sign, 3x3 diamond:  .#.
//                             #.#
//                             .#.
void drawRing(Framebuffer& fb, int x, int y, RGB c) {
  fb.set(x + 1, y, c);
  fb.set(x, y + 1, c);
  fb.set(x + 2, y + 1, c);
  fb.set(x + 1, y + 2, c);
}

// Where the temperature goes. Right-aligned to rightEdge, never further left
// than leftLimit. Tries, in order: decimal + degree sign, decimal, whole +
// degree sign, whole (the last one is used even if it doesn't fit).
struct TempLayout {
  std::string text;
  bool ring;
  int x;  // left edge of the first lit pixel (the minus sign or first digit)
};

TempLayout layoutTemp(int rightEdge, int leftLimit, int tenths, bool alwaysDecimal = false) {
  bool decimal = alwaysDecimal ? abs(tenths) < 1000 : abs(tenths) < 100;
  struct Option { bool dec, ring; };
  const Option opts[4] = {{true, true}, {true, false}, {false, true}, {false, false}};
  TempLayout out;
  for (int i = 0; i < 4; ++i) {
    if (opts[i].dec && !decimal) continue;
    out.text = tempLabel(tenths, opts[i].dec);
    out.ring = opts[i].ring;
    out.x = rightEdge - (text::tightWidth(out.text) + (out.ring ? 4 : 0));
    if (out.x >= leftLimit) break;
  }
  return out;
}

void drawTemp(Framebuffer& fb, const TempLayout& t, int y, RGB c) {
  int end = text::drawTight(fb, t.x, y, t.text, c);
  if (t.ring) drawRing(fb, end + 1, y + 1, c);
}

// Icon centred horizontally in the free space between leftEdge (first free
// column after the clock) and the temperature's first lit pixel.
int iconXBetween(int leftEdge, int tempX) {
  return leftEdge + (tempX - leftEdge - icons::kSize) / 2;
}

// One block next to the clock: weather (icon + outdoor temperature) or indoor
// (house + indoor temperature). Temperature right-aligned; the icon centred in
// the space left between leftEdge and the temperature (>= 1 px each side).
void drawBlock(Framebuffer& fb, const Screen& s, int which, int leftEdge, int iconY, int tempY) {
  if (which == Screen::kIndoor) {
    if (!s.inValid) return;
    TempLayout t = layoutTemp(fb.width(), leftEdge + 1 + icons::kSize + 1, s.inTenths, true);
    drawIcon(fb, iconXBetween(leftEdge, t.x), iconY, icons::kHome);
    drawTemp(fb, t, tempY, tempColor(s.inTenths));
    return;
  }
  if (s.wxValid) {
    TempLayout t = layoutTemp(fb.width(), leftEdge + 1 + icons::kSize + 1, s.tempTenths);
    drawIcon(fb, iconXBetween(leftEdge, t.x), iconY, s.icon);
    drawTemp(fb, t, tempY, tempColor(s.tempTenths));
  } else {
    text::draw(fb, fb.width() - text::width("--"), tempY, "--", kDim);
  }
}

// The front block moved down by wxSlide/1000 of the block height, the other
// one following from above, both clipped to rows [top, top + height).
void drawSlidingBlocks(Framebuffer& fb, const Screen& s, int leftEdge, int top, int height, int iconY,
                       int tempY) {
  int dy = s.wxSlide <= 0 ? 0 : s.wxSlide >= 1000 ? height : (s.wxSlide * height + 500) / 1000;
  int back = s.wxFront == Screen::kIndoor ? Screen::kOutdoor : Screen::kIndoor;
  fb.clipRows(top, top + height);
  if (dy < height) drawBlock(fb, s, s.wxFront, leftEdge, iconY + dy, tempY + dy);
  if (dy > 0) drawBlock(fb, s, back, leftEdge, iconY + dy - height, tempY + dy - height);
  fb.noClip();
}

int charsFor(int px) { return (px + 1) / text::kAdvance; }

// ---- 64x16 ---------------------------------------------------------------------
void home64(const Screen& s, Framebuffer& fb) {
  FontMetrics m = metrics(s.clockFont, false);
  int vw = clockVisibleWidth(m, s.timeValid, s.hh);
  int y = 1 + (14 - m.h) / 2;
  if (s.night) {
    drawClock(fb, s.clockFont, false, (fb.width() - vw) / 2, y, s.timeValid, s.hh, s.mm, kClockNight);
    return;
  }
  // The clock owns the left 30 px; H:MM is centred inside that slot.
  int clockEnd = drawClock(fb, s.clockFont, false, (30 - vw) / 2, y, s.timeValid, s.hh, s.mm, kClock);
  // The weather/indoor block owns everything right of the clock, full height.
  drawSlidingBlocks(fb, s, clockEnd, 0, fb.height(), (fb.height() - icons::kSize) / 2, 4);
}

// Line 1: time label in amber followed by the start of the title; line 2: rest.
void labelAndTitle(Framebuffer& fb, const std::string& label, const std::string& title,
                   int lines, RGB titleColor) {
  int perLine = charsFor(fb.width());
  int labelChars = text::length(label);
  int firstFree = perLine - labelChars - 1;
  std::vector<std::string> wrapped;
  int y = 0;
  if (firstFree >= 4) {
    wrapped = text::wrap(title, firstFree, perLine, lines);
    int x = text::draw(fb, 0, 0, label, kAccent);
    if (!wrapped.empty()) text::draw(fb, x + text::kAdvance, 0, wrapped[0], titleColor);
    for (size_t i = 1; i < wrapped.size(); ++i) text::draw(fb, 0, (int)i * text::kLineHeight, wrapped[i], titleColor);
  } else {
    text::draw(fb, 0, 0, label, kAccent);
    wrapped = text::wrap(title, perLine, perLine, lines - 1);
    y = text::kLineHeight;
    for (auto& l : wrapped) {
      text::draw(fb, 0, y, l, titleColor);
      y += text::kLineHeight;
    }
  }
}

void messageLines(Framebuffer& fb, const Screen& s, int linesPerPage) {
  int perLine = charsFor(fb.width());
  std::vector<std::string> lines = text::wrap(s.msg, perLine, perLine, 0);
  int start = s.msgPage * linesPerPage;
  for (int i = 0; i < linesPerPage && start + i < (int)lines.size(); ++i)
    text::draw(fb, 0, i * text::kLineHeight, lines[start + i], s.msgColor);
}

// ---- 32x32 ---------------------------------------------------------------------
void home32(const Screen& s, Framebuffer& fb) {
  FontMetrics m = metrics(s.clockFont, true);
  int x = (fb.width() - clockVisibleWidth(m, s.timeValid, s.hh)) / 2;
  if (s.night) {
    drawClock(fb, s.clockFont, true, x, (fb.height() - m.h) / 2, s.timeValid, s.hh, s.mm, kClockNight);
    return;
  }
  drawClock(fb, s.clockFont, true, x, 0, s.timeValid, s.hh, s.mm, kClock);
  int iy = m.h + 2;
  // The block is the icon row: clipped there so a slide never touches the
  // clock above or the next-event hint below.
  drawSlidingBlocks(fb, s, 0, iy, icons::kSize, iy, iy + 1);
  if (!s.nextLabel.empty() && iy + icons::kSize <= 25) {
    int tw = text::width(s.nextLabel);
    text::draw(fb, (fb.width() - tw) / 2, 24, s.nextLabel, kAccent);
  }
}

}  // namespace

const char* clockFontName(int font) {
  switch (font) {
    case kFontSegment: return "Segment";
    case kFontRounded: return "Rounded";
    case kFontSlim: return "Slim";
    case kFontDots: return "Dots";
    default: return "?";
  }
}

std::string tempLabel(int tenths, bool decimal) {
  char buf[16];
  if (decimal) {
    int a = abs(tenths);
    snprintf(buf, sizeof(buf), "%s%d.%d", tenths < 0 && a ? "-" : "", a / 10, a % 10);
  } else {
    int whole = tenths >= 0 ? (tenths + 5) / 10 : -((-tenths + 5) / 10);
    snprintf(buf, sizeof(buf), "%d", whole);
  }
  return buf;
}

int messagePages(const std::string& msg, int panelW, int panelH) {
  int perLine = charsFor(panelW);
  int linesPerPage = panelH / text::kLineHeight;
  int n = (int)text::wrap(msg, perLine, perLine, 0).size();
  int pages = (n + linesPerPage - 1) / linesPerPage;
  return pages < 1 ? 1 : pages;
}

void renderScreen(const Screen& s, Framebuffer& fb) {
  fb.clear();
  bool square = fb.height() >= 32;
  int lines = fb.height() / text::kLineHeight;
  switch (s.page) {
    case Screen::kHome:
      if (square) home32(s, fb); else home64(s, fb);
      break;
    case Screen::kEvent:
      labelAndTitle(fb, s.evLabel, s.evTitle, lines, kText);
      break;
    case Screen::kMessage:
      messageLines(fb, s, lines);
      break;
  }
}

int iconForWmo(int code, bool isDay) {
  switch (code) {
    case 0: return isDay ? icons::kSun : icons::kMoon;
    case 1: case 2: return isDay ? icons::kPartlyDay : icons::kPartlyNight;
    case 3: return icons::kCloud;
    case 45: case 48: return icons::kFog;
    case 51: case 53: case 55: case 56: case 57: return icons::kDrizzle;
    case 61: case 63: case 65: case 66: case 67:
    case 80: case 81: case 82: return icons::kRain;
    case 71: case 73: case 75: case 77: case 85: case 86: return icons::kSnow;
    case 95: case 96: case 99: return icons::kThunder;
    default: return icons::kCloud;
  }
}
