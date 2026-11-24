#include "indoor_temp.h"

#include "config_defaults.h"
#include "ntc.h"

namespace {
constexpr uint32_t kPeriodMs = 1000;
constexpr int kSamples = 16;
constexpr float kTauS = 30.0f;
// Node voltages outside this window mean a wiring fault, not a temperature:
// below ~0.1 V the NTC is shorted (> 100 degC), above ~3.0 V it's missing.
constexpr float kShortMv = 100.0f;
constexpr float kOpenMv = 3000.0f;
}  // namespace

bool IndoorTemp::begin() {
  enabled_ = NTC_PIN >= 0;
  if (!enabled_) return false;
  pinMode(NTC_PIN, INPUT);
  analogSetPinAttenuation(NTC_PIN, ADC_11db);  // 0..~3.1 V, most accurate 0.15..2.45 V
  return true;
}

void IndoorTemp::poll() {
  if (!enabled_ || millis() - lastMs_ < kPeriodMs) return;
  uint32_t now = millis();
  float dt = lastMs_ ? (now - lastMs_) / 1000.0f : kPeriodMs / 1000.0f;
  lastMs_ = now;

  uint32_t sum = 0;
  for (int i = 0; i < kSamples; ++i) sum += analogReadMilliVolts(NTC_PIN);
  mv_ = (float)sum / kSamples;

  if (mv_ < kShortMv) {
    fault_ = "short (or above 100 C)";
    return;
  }
  if (mv_ > kOpenMv) {
    fault_ = "open - NTC not connected";
    return;
  }
  ohms_ = ntc::ohms(mv_, NTC_VCC_MV, NTC_R_SERIES);
  float c = ntc::celsius(ohms_, NTC_R25, NTC_BETA);
  if (isnan(c)) {
    fault_ = "bad reading";
    return;
  }
  bool wasFault = *fault_ != '\0';
  fault_ = "";
  if (!have_ || wasFault) {
    ema_ = c;  // start from the first good reading instead of fading in
    have_ = true;
    return;
  }
  ema_ += (1.0f - expf(-dt / kTauS)) * (c - ema_);
}

int IndoorTemp::shownTenths(int offsetTenths) {
  float t = ema_ * 10.0f + offsetTenths;
  int candidate = (int)lroundf(t);
  // Only move when the value is clearly past the rounding edge (0.06 degC
  // beyond it), so 21.45 doesn't toggle between 21.4 and 21.5 every second.
  if (!shownSet_ || (candidate != shown_ && fabsf(t - shown_) > 0.56f)) {
    shown_ = candidate;
    shownSet_ = true;
  }
  return shown_;
}
