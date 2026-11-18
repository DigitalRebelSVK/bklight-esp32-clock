// Lux -> panel brightness with smoothing and hysteresis. Portable.
#pragma once
#include <math.h>
#include <stdint.h>

struct AutoBrightConfig {
  float luxDim = 1.0f;       // at or below: minimum brightness
  float luxBright = 400.0f;  // at or above: maximum brightness
  uint8_t minLevel = 4;      // 0..100 panel units
  uint8_t maxLevel = 100;
  uint8_t step = 4;          // ignore changes smaller than this (hysteresis)
  float nightOnLux = 0.8f;   // enter night mode below this
  float nightOffLux = 3.0f;  // leave night mode above this
  float tauUpS = 1.5f;       // react fast when the lights come on
  float tauDownS = 8.0f;     // fade slowly so a passing shadow does nothing
};

class AutoBright {
 public:
  explicit AutoBright(const AutoBrightConfig& c = AutoBrightConfig()) : cfg_(c) {}

  // Feed a raw lux sample; dtS = seconds since the previous sample.
  void update(float lux, float dtS) {
    float l = log10f(lux < 0.01f ? 0.01f : lux);
    if (!primed_) {
      logLux_ = l;
      primed_ = true;
    } else {
      float tau = l > logLux_ ? cfg_.tauUpS : cfg_.tauDownS;
      float a = 1.0f - expf(-dtS / tau);
      logLux_ += a * (l - logLux_);
    }
    float smooth = powf(10.0f, logLux_);

    if (night_ && smooth > cfg_.nightOffLux) night_ = false;
    else if (!night_ && smooth < cfg_.nightOnLux) night_ = true;

    float lo = log10f(cfg_.luxDim), hi = log10f(cfg_.luxBright);
    float x = (logLux_ - lo) / (hi - lo);
    if (x < 0) x = 0;
    if (x > 1) x = 1;
    int target = (int)lroundf(cfg_.minLevel + x * (cfg_.maxLevel - cfg_.minLevel));
    int diff = target - (int)level_;
    bool atEdge = target == cfg_.minLevel || target == cfg_.maxLevel;
    if (!haveLevel_ || diff >= cfg_.step || diff <= -cfg_.step || (atEdge && diff != 0)) {
      level_ = (uint8_t)target;
      haveLevel_ = true;
    }
  }

  bool ready() const { return haveLevel_; }
  uint8_t level() const { return level_; }
  bool night() const { return night_; }
  float smoothedLux() const { return primed_ ? powf(10.0f, logLux_) : 0.0f; }
  const AutoBrightConfig& config() const { return cfg_; }

 private:
  AutoBrightConfig cfg_;
  bool primed_ = false;
  bool haveLevel_ = false;
  bool night_ = false;
  float logLux_ = 0;
  uint8_t level_ = 50;
};
