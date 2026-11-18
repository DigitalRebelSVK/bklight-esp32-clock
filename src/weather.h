// Current weather from Open-Meteo (no API key).
#pragma once
#include <Arduino.h>

class Weather {
 public:
  // Call often; fetches every WEATHER_REFRESH_MIN (2 min retry after errors).
  void update(bool wifiUp);
  bool valid() const;
  int tempC() const { return (int)lroundf(temp_); }
  float tempExact() const { return temp_; }
  int code() const { return code_; }
  bool isDay() const { return isDay_; }
  const String& status() const { return status_; }

 private:
  bool fetch();
  float temp_ = 0;
  int code_ = 0;
  bool isDay_ = true;
  bool have_ = false;
  uint32_t lastOk_ = 0;
  uint32_t nextTry_ = 0;
  String status_ = "not fetched yet";
};
