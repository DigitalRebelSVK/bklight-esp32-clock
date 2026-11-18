#include "text.h"

#include "font5x8.h"

namespace text {
namespace {

const uint8_t* glyphRows(uint32_t cp) {
  int lo = 0, hi = font5x8::kCount - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    uint32_t c = font5x8::kCodes[mid];
    if (c == cp) return font5x8::kRows[mid];
    if (c < cp) lo = mid + 1; else hi = mid - 1;
  }
  return nullptr;
}

// Typographic characters iOS puts into titles that the font may lack.
uint32_t fallback(uint32_t cp) {
  switch (cp) {
    case 0x00A0: return ' ';
    case 0x2010: case 0x2011: case 0x2012: return '-';
    case 0x201A: case 0x201B: return '\'';
    case 0x201E: case 0x201F: return '"';
    default: return '?';
  }
}

std::vector<uint32_t> decode(const std::string& s) {
  std::vector<uint32_t> out;
  const char* p = s.c_str();
  while (*p) out.push_back(nextCodepoint(&p));
  return out;
}

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += (char)cp;
  } else if (cp < 0x800) {
    out += (char)(0xC0 | (cp >> 6));
    out += (char)(0x80 | (cp & 0x3F));
  } else {
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
  }
}

std::string encode(const std::vector<uint32_t>& cps, size_t from, size_t to) {
  std::string out;
  for (size_t i = from; i < to && i < cps.size(); ++i) appendUtf8(out, cps[i]);
  return out;
}

}  // namespace

uint32_t nextCodepoint(const char** s) {
  const unsigned char* p = (const unsigned char*)*s;
  uint32_t cp;
  int extra;
  if (p[0] < 0x80) { cp = p[0]; extra = 0; }
  else if ((p[0] & 0xE0) == 0xC0) { cp = p[0] & 0x1F; extra = 1; }
  else if ((p[0] & 0xF0) == 0xE0) { cp = p[0] & 0x0F; extra = 2; }
  else if ((p[0] & 0xF8) == 0xF0) { cp = p[0] & 0x07; extra = 3; }
  else { *s += 1; return '?'; }
  for (int i = 1; i <= extra; ++i) {
    if ((p[i] & 0xC0) != 0x80) { *s += i; return '?'; }
    cp = (cp << 6) | (p[i] & 0x3F);
  }
  *s += 1 + extra;
  return cp;
}

int length(const std::string& s) { return (int)decode(s).size(); }

int width(const std::string& s) {
  int n = length(s);
  return n ? n * kAdvance - 1 : 0;
}

int draw(Framebuffer& fb, int x, int y, const std::string& s, RGB color) {
  const char* p = s.c_str();
  while (*p) {
    uint32_t cp = nextCodepoint(&p);
    const uint8_t* rows = glyphRows(cp);
    if (!rows) rows = glyphRows(fallback(cp));
    if (!rows) rows = glyphRows('?');
    for (int r = 0; r < font5x8::kHeight; ++r)
      for (int c = 0; c < kAdvance; ++c)
        if (rows[r] & (0x80 >> c)) fb.set(x + c, y + r, color);
    x += kAdvance;
  }
  return x;
}

namespace {
const uint8_t* rowsFor(uint32_t cp) {
  const uint8_t* rows = glyphRows(cp);
  if (!rows) rows = glyphRows(fallback(cp));
  if (!rows) rows = glyphRows('?');
  return rows;
}

// First and last lit column of a glyph; {0,-1} for blank glyphs.
void inkSpan(const uint8_t* rows, int* first, int* last) {
  *first = kAdvance;
  *last = -1;
  for (int r = 0; r < font5x8::kHeight; ++r)
    for (int c = 0; c < kAdvance; ++c)
      if (rows[r] & (0x80 >> c)) {
        if (c < *first) *first = c;
        if (c > *last) *last = c;
      }
}
}  // namespace

// Bottom row of the digits; the decimal point is a single LED there (the
// font's own '.' is a 3x3 plus that eats 2 px of a 20 px budget).
int digitBaseRow() {
  const uint8_t* rows = rowsFor('0');
  int last = 0;
  for (int r = 0; r < font5x8::kHeight; ++r)
    if (rows[r]) last = r;
  return last;
}

int tightWidth(const std::string& s) {
  const char* p = s.c_str();
  int w = 0, n = 0;
  while (*p) {
    uint32_t cp = nextCodepoint(&p);
    int a, b;
    inkSpan(rowsFor(cp), &a, &b);
    if (cp == '.') w += 1;
    else w += b >= a ? b - a + 1 : 2;  // blank glyph (space) = 2 px
    ++n;
  }
  return n ? w + (n - 1) : 0;
}

int drawTight(Framebuffer& fb, int x, int y, const std::string& s, RGB color) {
  const char* p = s.c_str();
  bool firstGlyph = true;
  while (*p) {
    uint32_t cp = nextCodepoint(&p);
    const uint8_t* rows = rowsFor(cp);
    int a, b;
    inkSpan(rows, &a, &b);
    if (!firstGlyph) x += 1;
    firstGlyph = false;
    if (cp == '.') {
      fb.set(x, y + digitBaseRow(), color);
      x += 1;
      continue;
    }
    if (b < a) {
      x += 2;
      continue;
    }
    for (int r = 0; r < font5x8::kHeight; ++r)
      for (int c = a; c <= b; ++c)
        if (rows[r] & (0x80 >> c)) fb.set(x + c - a, y + r, color);
    x += b - a + 1;
  }
  return x;
}

std::vector<std::string> wrap(const std::string& s, int firstWidth, int otherWidth, int maxLines) {
  std::vector<uint32_t> cps = decode(s);
  std::vector<std::vector<uint32_t>> lines;
  std::vector<uint32_t> cur;
  size_t i = 0;
  auto limit = [&]() { return lines.empty() ? firstWidth : otherWidth; };

  while (i < cps.size()) {
    while (i < cps.size() && cps[i] == ' ') ++i;  // words are re-joined with one space
    size_t j = i;
    while (j < cps.size() && cps[j] != ' ') ++j;
    std::vector<uint32_t> word(cps.begin() + i, cps.begin() + j);
    if (word.empty()) break;
    int need = (int)word.size() + (cur.empty() ? 0 : 1);
    if ((int)cur.size() + need <= limit()) {
      if (!cur.empty()) cur.push_back(' ');
      cur.insert(cur.end(), word.begin(), word.end());
      i = j;
    } else if (cur.empty()) {
      // Word longer than a whole line: hard split, with a hyphen when there
      // is room for one.
      int lim = limit() > 0 ? limit() : 1;
      int take = lim >= 4 ? lim - 1 : lim;
      cur.assign(word.begin(), word.begin() + take);
      if (take < lim) cur.push_back('-');
      lines.push_back(cur);
      cur.clear();
      i += take;
    } else {
      lines.push_back(cur);
      cur.clear();
    }
  }
  if (!cur.empty()) lines.push_back(cur);

  if (maxLines > 0 && (int)lines.size() > maxLines) {
    // Fill the last visible line with as much of the remaining text as fits,
    // instead of stopping at a word boundary, then end with an ellipsis.
    std::vector<uint32_t> last;
    for (size_t k = maxLines - 1; k < lines.size(); ++k) {
      if (!last.empty()) last.push_back(' ');
      last.insert(last.end(), lines[k].begin(), lines[k].end());
    }
    lines.resize(maxLines);
    int lim = maxLines == 1 ? firstWidth : otherWidth;
    if (lim < 1) lim = 1;
    while ((int)last.size() > lim - 1) last.pop_back();
    while (!last.empty() && last.back() == ' ') last.pop_back();
    last.push_back(0x2026);  // ellipsis
    lines.back() = last;
  }

  std::vector<std::string> out;
  for (auto& l : lines) out.push_back(encode(l, 0, l.size()));
  return out;
}

std::string truncate(const std::string& s, int maxChars) {
  std::vector<uint32_t> cps = decode(s);
  return encode(cps, 0, (size_t)maxChars);
}

std::string upper(const std::string& s) {
  std::vector<uint32_t> cps = decode(s);
  for (auto& c : cps) {
    if (c >= 'a' && c <= 'z') {
      c -= 0x20;
    } else if (c >= 0xE0 && c <= 0xFE && c != 0xF7) {
      c -= 0x20;  // Latin-1 lowercase block
    } else if (c >= 0x100 && c <= 0x17F) {
      // Latin Extended-A pairs: lower = upper + 1, but the parity flips in
      // 0x139..0x148 and 0x179..0x17E (Ĺĺ Ľľ Ňň ... Žž).
      bool oddUpper = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
      bool isLower = oddUpper ? (c % 2 == 0) : (c % 2 == 1);
      if (isLower && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) c -= 1;
    }
  }
  return encode(cps, 0, cps.size());
}

}  // namespace text
