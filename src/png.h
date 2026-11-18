// Minimal PNG encoder (RGB888, fixed-Huffman deflate with LZ77) and the
// BK-Light frame wrapper. Portable, no heap allocation.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace png {

// Standard CRC-32 (same as zlib.crc32 / binascii.crc32).
uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0);

// Encodes w*h RGB888 pixels into out. Returns the PNG size, or 0 if it does
// not fit (raw scanlines are limited to kMaxRaw bytes, enough for 1024 px).
constexpr size_t kMaxRaw = 4096;
size_t encode(const uint8_t* rgb, int w, int h, uint8_t* out, size_t outCap);

}  // namespace png

namespace bkframe {

// Wraps a PNG the way Bk-Light-AppBypass build_frame() does:
//   len16(N+15) 02 00 00 N32 crc32(png) 00 <slot> png
// The repo always uses slot 0x65; pypixelcolor uses 0x00 for "show, don't save".
size_t build(const uint8_t* png, size_t n, uint8_t slot, uint8_t* out, size_t outCap);

}  // namespace bkframe
