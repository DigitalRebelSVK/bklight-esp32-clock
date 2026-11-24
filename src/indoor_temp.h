// Indoor thermometer: 100k NTC in a divider on an ADC1 pin (see README).
// Sampled once a second, smoothed with a 30 s time constant, displayed with a
// little hysteresis so the last digit doesn't flicker between two values.
#pragma once
#include <Arduino.h>

class IndoorTemp {
 public:
  bool begin();  // false when NTC_PIN < 0 (no thermometer)
  bool enabled() const { return enabled_; }
  void poll();   // call every loop; samples once a second, takes < 1 ms

  bool valid() const { return enabled_ && have_ && !*fault_; }
  // Smoothed temperature in 0.1 degC with calibration offset and display
  // hysteresis applied.
  int shownTenths(int offsetTenths);
  float rawCelsius() const { return ema_; }  // smoothed, no offset
  float millivolts() const { return mv_; }
  float ohms() const { return ohms_; }
  const char* fault() const { return fault_; }  // "" when fine

 private:
  bool enabled_ = false;
  bool have_ = false;
  float ema_ = 0, mv_ = 0, ohms_ = 0;
  int shown_ = 0;
  bool shownSet_ = false;
  uint32_t lastMs_ = 0;
  const char* fault_ = "";
};
