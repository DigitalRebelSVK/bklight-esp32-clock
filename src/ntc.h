// NTC thermistor maths. Portable (host tests use it too).
//
// Wiring: 3V3 -- R_series -- node -- NTC -- GND, node -> ADC.
// Warmer -> NTC resistance falls -> node voltage falls.
#pragma once
#include <math.h>

namespace ntc {

// NTC resistance from the node voltage. Negative when the reading makes no
// sense (node at or above the supply = NTC missing).
inline float ohms(float nodeMv, float supplyMv, float rSeries) {
  if (nodeMv <= 0.0f) return 0.0f;
  if (nodeMv >= supplyMv) return -1.0f;
  return rSeries * nodeMv / (supplyMv - nodeMv);
}

// Beta equation: 1/T = 1/T25 + ln(R/R25)/B, T in kelvin.
inline float celsius(float r, float r25, float beta) {
  if (r <= 0.0f) return NAN;
  float invT = 1.0f / 298.15f + logf(r / r25) / beta;
  return 1.0f / invT - 273.15f;
}

// Inverse, for tests: resistance at a temperature.
inline float ohmsAt(float c, float r25, float beta) {
  return r25 * expf(beta * (1.0f / (c + 273.15f) - 1.0f / 298.15f));
}

// Node voltage the divider gives at a resistance, for tests.
inline float nodeMv(float r, float supplyMv, float rSeries) { return supplyMv * r / (rSeries + r); }

}  // namespace ntc
