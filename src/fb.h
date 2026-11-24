// Tiny RGB888 framebuffer. Portable (no Arduino deps) so it also builds on a PC
// for the preview tool.
#pragma once
#include <stdint.h>
#include <string.h>

struct RGB {
  uint8_t r, g, b;
};

class Framebuffer {
 public:
  static constexpr int kMaxPixels = 64 * 64;

  Framebuffer(int w, int h) : w_(w), h_(h), clipY0_(0), clipY1_(h) { clear(); }

  int width() const { return w_; }
  int height() const { return h_; }
  size_t bytes() const { return (size_t)w_ * h_ * 3; }
  const uint8_t* data() const { return px_; }

  void clear(RGB c = {0, 0, 0}) {
    for (int i = 0; i < w_ * h_; ++i) {
      px_[i * 3 + 0] = c.r;
      px_[i * 3 + 1] = c.g;
      px_[i * 3 + 2] = c.b;
    }
  }

  // Only rows y0 <= y < y1 can be drawn until noClip(). Used to slide a
  // block of the screen without touching what is above or below it.
  void clipRows(int y0, int y1) {
    clipY0_ = y0 < 0 ? 0 : y0;
    clipY1_ = y1 > h_ ? h_ : y1;
  }
  void noClip() {
    clipY0_ = 0;
    clipY1_ = h_;
  }

  void set(int x, int y, RGB c) {
    if (x < 0 || x >= w_ || y < clipY0_ || y >= clipY1_) return;
    uint8_t* p = px_ + (y * w_ + x) * 3;
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
  }

  void fillRect(int x, int y, int w, int h, RGB c) {
    for (int yy = y; yy < y + h; ++yy)
      for (int xx = x; xx < x + w; ++xx) set(xx, yy, c);
  }

  // Software dimming, only used when the panel's hardware brightness command
  // is disabled. Keeps lit pixels at >= 1 so thin strokes do not vanish.
  void scale(float f) {
    if (f >= 0.999f) return;
    for (size_t i = 0; i < bytes(); ++i) {
      if (!px_[i]) continue;
      int v = (int)(px_[i] * f + 0.5f);
      px_[i] = (uint8_t)(v < 1 ? 1 : v);
    }
  }

 private:
  int w_, h_;
  int clipY0_, clipY1_;
  uint8_t px_[kMaxPixels * 3];
};
