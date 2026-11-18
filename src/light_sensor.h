// Ambient light: VEML7700 on I2C, or a phototransistor (3DU33) on the ADC.
// Which one is chosen by LIGHT_SENSOR in clock_config.h / config_defaults.h.
#pragma once
#include <Arduino.h>

class LightSensor {
 public:
  bool begin();
  bool present() const { return kind_ != kNone; }
  const char* name() const;
  // Non-blocking apart from ~20 ms of ADC averaging. True when a fresh lux
  // value was written.
  bool poll(float* lux);
  // Diagnostics for the status page: VEML counts or phototransistor mV,
  // the photo current in uA (phototransistor only) and the active range.
  float raw() const { return raw_; }
  float microamps() const { return microamps_; }
  const char* range() const { return sensitive_ ? "dark" : "bright"; }

 private:
  enum Kind { kNone, kVeml, kPhoto };
  bool vemlConfig(bool sensitive);
  bool vemlRead(uint16_t* raw);
  bool vemlPoll(float* lux);
  void photoRange(bool dark);
  bool photoPoll(float* lux);

  Kind kind_ = kNone;
  bool sensitive_ = true;
  uint32_t settleUntil_ = 0;
  uint32_t nextRead_ = 0;
  float raw_ = 0;
  float microamps_ = 0;
};
