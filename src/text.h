// UTF-8 text on the 5x8 font, plus word wrapping. Portable.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

#include "fb.h"

namespace text {

constexpr int kAdvance = 5;   // px per character incl. 1 px gap
constexpr int kLineHeight = 8;

// Decodes one code point from a UTF-8 string and advances *s. Invalid bytes
// come back as '?'.
uint32_t nextCodepoint(const char** s);

// Number of code points in the string.
int length(const std::string& s);

// Width in pixels of the lit part of the text (no trailing gap).
int width(const std::string& s);

// Draws text with its top-left at (x, y). Returns x after the last glyph.
int draw(Framebuffer& fb, int x, int y, const std::string& s, RGB color);

// "Tight" text: every glyph trimmed to its lit columns plus 1 px spacing, so
// "-9.7" is 16 px wide instead of 19. Used for the temperature.
int tightWidth(const std::string& s);
int drawTight(Framebuffer& fb, int x, int y, const std::string& s, RGB color);

// Greedy word wrap. firstWidth/otherWidth are in characters. Words longer than
// a line are hard-split. When more than maxLines would be needed, the last
// line is cut and ends with an ellipsis. maxLines <= 0 means unlimited.
std::vector<std::string> wrap(const std::string& s, int firstWidth, int otherWidth, int maxLines);

// Keeps at most maxChars code points (used to cap stored titles).
std::string truncate(const std::string& s, int maxChars);

// Upper-cases ASCII, Latin-1 and Latin Extended-A (all Slovak/Czech letters).
// Capitals are 6 px tall in this font vs a 4 px x-height, so caps read far
// better from across the room.
std::string upper(const std::string& s);

}  // namespace text
