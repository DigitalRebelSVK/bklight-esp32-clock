// Timeline of the once-a-minute indoor temperature slide. Portable, so the
// preview tool renders exactly what the clock sends.
//
//   0 .. S            weather slides down, indoor comes in from above
//   S .. S+H          indoor temperature stays
//   S+H .. 2S+H       indoor slides down, weather comes in from above
//   rest of minute    weather
//
// S = slideMs, H = holdMs, times in ms since the start of the minute.
#pragma once

#include <math.h>

#include "render.h"

struct SlideState {
  int front = Screen::kOutdoor;  // Screen::wxFront
  int slide = 0;                 // Screen::wxSlide, 0..1000
  bool busy = false;             // inside the animation window: keep the loop free
};

// Sine ease-in-out of p in 0..1000 -> 0..1000. Gentler than cubic: over BLE
// the panel takes maybe 5-8 frames a second, and cubic would spend the first
// and last of those on 1-px moves and leave a big jump in the middle.
inline int easeInOut(int p) {
  if (p <= 0) return 0;
  if (p >= 1000) return 1000;
  float y = (1.0f - cosf(3.14159265f * p / 1000.0f)) / 2.0f;
  return (int)(y * 1000 + 0.5f);
}

// enabled: daytime, setting on, both temperatures valid.
inline SlideState slideAt(int msInMinute, bool enabled, int slideMs, int holdMs) {
  SlideState st;
  if (!enabled) return st;
  int t = msInMinute;
  if (t < slideMs) {
    st.front = Screen::kOutdoor;
    st.slide = easeInOut(t * 1000 / slideMs);
    st.busy = true;
  } else if (t < slideMs + holdMs) {
    st.front = Screen::kIndoor;
    st.busy = true;
  } else if (t < 2 * slideMs + holdMs) {
    st.front = Screen::kIndoor;
    st.slide = easeInOut((t - slideMs - holdMs) * 1000 / slideMs);
    st.busy = true;
  }
  return st;
}
