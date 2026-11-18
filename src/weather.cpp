#include "weather.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "config_defaults.h"

bool Weather::valid() const {
  return have_ && millis() - lastOk_ < (uint32_t)WEATHER_STALE_MIN * 60000UL;
}

void Weather::update(bool wifiUp) {
  if (!wifiUp || (int32_t)(millis() - nextTry_) < 0) return;
  bool ok = fetch();
  nextTry_ = millis() + (ok ? (uint32_t)WEATHER_REFRESH_MIN * 60000UL : 120000UL);
}

bool Weather::fetch() {
  char url[200];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,weather_code,is_day&timezone=auto",
           LATITUDE, LONGITUDE);
  WiFiClientSecure tls;
  tls.setInsecure();  // public, read-only data; skips CA bundle maintenance
  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin(tls, url)) {
    status_ = "http.begin failed";
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    status_ = "HTTP " + String(code);
    http.end();
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getString());
  http.end();
  if (err) {
    status_ = String("JSON ") + err.c_str();
    return false;
  }
  JsonObject cur = doc["current"];
  if (cur.isNull() || cur["temperature_2m"].isNull()) {
    status_ = "no 'current' block";
    return false;
  }
  temp_ = cur["temperature_2m"].as<float>();
  code_ = cur["weather_code"].as<int>();
  isDay_ = cur["is_day"].as<int>() != 0;
  have_ = true;
  lastOk_ = millis();
  status_ = "ok " + String(temp_, 1) + " C, code " + String(code_);
  return true;
}
