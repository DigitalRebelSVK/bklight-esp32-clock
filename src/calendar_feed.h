// Pulls a public calendar link (.ics / webcal://) over HTTPS and fills the
// clock's Calendar with today's and tomorrow's events.
#pragma once
#include <Arduino.h>

#include "calendar.h"

class CalendarFeed {
 public:
  bool enabled() const;
  // Call often. Fetches every CAL_REFRESH_MIN, right after midnight, and
  // retries after 2 min on errors. Only starts a fetch early in a minute so
  // the blocking download never delays the clock's minute change.
  void update(bool wifiUp, Calendar& cal);
  void requestNow() { nextTry_ = 0; }
  const String& status() const { return status_; }

 private:
  bool fetch(Calendar& cal);
  uint32_t nextTry_ = 0;
  long lastDay_ = -1;
  String status_ = "no link in clock_config.h (CAL_ICS_URL)";
};
