#include "png.h"

#include <string.h>

namespace png {
namespace {

uint32_t gCrcTable[256];
bool gCrcReady = false;

void initCrc() {
  for (uint32_t n = 0; n < 256; ++n) {
    uint32_t c = n;
    for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    gCrcTable[n] = c;
  }
  gCrcReady = true;
}

// ---- bit writer (deflate is LSB-first) ------------------------------------
struct BitWriter {
  BitWriter(uint8_t* o, size_t c) : out(o), cap(c) {}
  uint8_t* out;
  size_t cap;
  size_t pos = 0;
  uint32_t acc = 0;
  int nbits = 0;
  bool overflow = false;

  void bits(uint32_t v, int n) {
    acc |= v << nbits;
    nbits += n;
    while (nbits >= 8) {
      if (pos < cap) out[pos++] = (uint8_t)acc; else overflow = true;
      acc >>= 8;
      nbits -= 8;
    }
  }
  // Huffman codes are defined MSB-first, so reverse before writing.
  void huff(uint32_t code, int len) {
    uint32_t r = 0;
    for (int i = 0; i < len; ++i) r |= ((code >> i) & 1u) << (len - 1 - i);
    bits(r, len);
  }
  void flush() {
    if (nbits > 0) bits(0, 8 - nbits);
  }
};

void litLen(BitWriter& bw, int sym) {
  if (sym < 144) bw.huff(0x30 + sym, 8);
  else if (sym < 256) bw.huff(0x190 + (sym - 144), 9);
  else if (sym < 280) bw.huff(sym - 256, 7);
  else bw.huff(0xC0 + (sym - 280), 8);
}

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                               2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void match(BitWriter& bw, int len, int dist) {
  int li = 28;
  while (li > 0 && kLenBase[li] > len) --li;
  litLen(bw, 257 + li);
  if (kLenExtra[li]) bw.bits(len - kLenBase[li], kLenExtra[li]);
  int di = 29;
  while (di > 0 && kDistBase[di] > dist) --di;
  bw.huff(di, 5);
  if (kDistExtra[di]) bw.bits(dist - kDistBase[di], kDistExtra[di]);
}

// ---- LZ77 with hash chains (positions stored +1, 0 = empty) ---------------
constexpr int kHashBits = 11;
constexpr int kHashSize = 1 << kHashBits;
constexpr int kMaxChain = 48;
uint16_t gHead[kHashSize];
uint16_t gPrev[kMaxRaw];
uint8_t gRaw[kMaxRaw];

inline int hash3(const uint8_t* p) {
  return ((p[0] << 10) ^ (p[1] << 5) ^ p[2]) & (kHashSize - 1);
}

// zlib stream: header, one fixed-Huffman block, adler32.
size_t zlibCompress(const uint8_t* in, size_t n, uint8_t* out, size_t cap) {
  BitWriter bw(out, cap);
  bw.bits(0x78, 8);
  bw.bits(0x01, 8);
  bw.bits(1, 1);  // BFINAL
  bw.bits(1, 2);  // BTYPE = fixed Huffman

  memset(gHead, 0, sizeof(gHead));
  auto insert = [&](size_t pos) {
    if (pos + 2 >= n) return;
    int h = hash3(in + pos);
    gPrev[pos] = gHead[h];
    gHead[h] = (uint16_t)(pos + 1);
  };

  size_t i = 0;
  while (i < n) {
    int bestLen = 0, bestDist = 0;
    if (i + 2 < n) {
      int chain = kMaxChain;
      uint16_t cand = gHead[hash3(in + i)];
      size_t maxLen = n - i < 258 ? n - i : 258;
      while (cand && chain--) {
        size_t c = cand - 1;
        size_t l = 0;
        while (l < maxLen && in[c + l] == in[i + l]) ++l;
        if ((int)l > bestLen) {
          bestLen = (int)l;
          bestDist = (int)(i - c);
          if (l == maxLen) break;
        }
        cand = gPrev[c];
      }
    }
    if (bestLen >= 3) {
      match(bw, bestLen, bestDist);
      for (int k = 0; k < bestLen; ++k) insert(i + k);
      i += bestLen;
    } else {
      litLen(bw, in[i]);
      insert(i);
      ++i;
    }
  }
  litLen(bw, 256);  // end of block
  bw.flush();

  uint32_t a = 1, b = 0;
  for (size_t k = 0; k < n; ++k) {
    a = (a + in[k]) % 65521;
    b = (b + a) % 65521;
  }
  uint32_t adler = (b << 16) | a;
  for (int s = 24; s >= 0; s -= 8) bw.bits((adler >> s) & 0xFF, 8);
  return bw.overflow ? 0 : bw.pos;
}

void put32be(uint8_t* p, uint32_t v) {
  p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

// Writes length + type + data (data must already be at p+8) + CRC.
size_t finishChunk(uint8_t* p, const char* type, size_t dataLen) {
  put32be(p, (uint32_t)dataLen);
  memcpy(p + 4, type, 4);
  put32be(p + 8 + dataLen, crc32(p + 4, dataLen + 4));
  return dataLen + 12;
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc) {
  if (!gCrcReady) initCrc();
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) crc = gCrcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  return ~crc;
}

size_t encode(const uint8_t* rgb, int w, int h, uint8_t* out, size_t outCap) {
  size_t stride = (size_t)w * 3;
  size_t rawLen = (stride + 1) * h;
  if (rawLen > kMaxRaw || outCap < 64) return 0;
  for (int y = 0; y < h; ++y) {
    gRaw[y * (stride + 1)] = 0;  // filter: none
    memcpy(gRaw + y * (stride + 1) + 1, rgb + y * stride, stride);
  }

  static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  size_t pos = 0;
  memcpy(out, kSig, 8);
  pos += 8;

  uint8_t* ihdr = out + pos;
  put32be(ihdr + 8, (uint32_t)w);
  put32be(ihdr + 12, (uint32_t)h);
  ihdr[16] = 8;  // bit depth
  ihdr[17] = 2;  // colour type RGB
  ihdr[18] = ihdr[19] = ihdr[20] = 0;
  pos += finishChunk(ihdr, "IHDR", 13);

  if (pos + 12 + 12 >= outCap) return 0;
  size_t zlen = zlibCompress(gRaw, rawLen, out + pos + 8, outCap - pos - 12 - 12);
  if (!zlen) return 0;
  pos += finishChunk(out + pos, "IDAT", zlen);

  pos += finishChunk(out + pos, "IEND", 0);
  return pos;
}

}  // namespace png

namespace bkframe {

size_t build(const uint8_t* pngData, size_t n, uint8_t slot, uint8_t* out, size_t outCap) {
  if (n + 15 > outCap || n + 15 > 0xFFFF) return 0;
  uint32_t total = (uint32_t)n + 15;
  uint32_t crc = png::crc32(pngData, n);
  size_t i = 0;
  out[i++] = total & 0xFF;
  out[i++] = (total >> 8) & 0xFF;
  out[i++] = 0x02;
  out[i++] = 0x00;
  out[i++] = 0x00;
  for (int s = 0; s < 32; s += 8) out[i++] = (n >> s) & 0xFF;
  for (int s = 0; s < 32; s += 8) out[i++] = (crc >> s) & 0xFF;
  out[i++] = 0x00;
  out[i++] = slot;
  memcpy(out + i, pngData, n);
  return i + n;
}

}  // namespace bkframe
