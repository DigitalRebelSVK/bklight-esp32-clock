#include "calendar_feed.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "config_defaults.h"
#include "ics.h"

namespace {

void utcToLocal(long long epoch, long* days, int* minute) {
  time_t t = (time_t)epoch;
  struct tm lt;
  localtime_r(&t, &lt);
  *days = daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
  *minute = lt.tm_hour * 60 + lt.tm_min;
}

// Body is read into this static buffer instead of HTTPClient::writeToStream(),
// which mallocs 4 KB per block. While TLS holds ~40 KB the heap is fragmented
// and that malloc sometimes fails ("download cut off (-8)").
uint8_t rxBuf[1024];

// Reads the whole body into the parser. HTTP/1.0 is requested, so the server
// sends it unchunked: either Content-Length bytes or until it closes.
bool readBody(HTTPClient& http, IcsReader& reader, String* err) {
  auto* s = http.getStreamPtr();  // WiFiClient on core 2.x, NetworkClient on 3.x
  int remaining = http.getSize();  // -1 = unknown, read until close
  uint32_t lastData = millis();
  while (remaining != 0) {
    int avail = s->available();
    if (avail > 0) {
      int want = avail < (int)sizeof(rxBuf) ? avail : (int)sizeof(rxBuf);
      if (remaining > 0 && want > remaining) want = remaining;
      int n = s->read(rxBuf, want);
      if (n > 0) {
        reader.feed((const char*)rxBuf, n);
        if (remaining > 0) remaining -= n;
        lastData = millis();
      }
    } else if (!s->connected()) {
      break;
    } else if (millis() - lastData > 15000) {
      *err = "download stalled";
      return false;
    } else {
      delay(5);
    }
  }
  if (remaining > 0) {
    *err = "download cut off (" + String(remaining) + " B missing)";
    return false;
  }
  return true;
}

String httpsUrl(String u) {
  u.trim();
  if (u.startsWith("webcal://")) u = "https://" + u.substring(9);
  return u;
}

}  // namespace

bool CalendarFeed::enabled() const { return strlen(CAL_ICS_URL) > 0; }

void CalendarFeed::update(bool wifiUp, Calendar& cal) {
  if (!enabled() || !wifiUp) return;
  struct tm t;
  time_t now = time(nullptr);
  localtime_r(&now, &t);
  if (t.tm_year + 1900 < 2024) return;  // need the date first
  long today = daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  bool newDay = lastDay_ >= 0 && today != lastDay_;
  bool due = (int32_t)(millis() - nextTry_) >= 0;
  if (!due && !newDay) return;
  if (t.tm_sec < 3 || t.tm_sec > 25) return;  // stay clear of the minute change
  bool ok = fetch(cal);
  lastDay_ = today;
  nextTry_ = millis() + (ok ? (uint32_t)CAL_REFRESH_MIN * 60000UL : 120000UL);
}

bool CalendarFeed::fetch(Calendar& cal) {
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  long today = daysFromCivil(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  IcsReader reader(today, 2, utcToLocal);
  uint32_t t0 = millis();

  const char* urls[2] = {CAL_ICS_URL, CAL_ICS_URL2};
  int fetched = 0;
  for (const char* raw : urls) {
    if (!raw || !strlen(raw)) continue;
    WiFiClientSecure tls;
    tls.setInsecure();  // read-only data; the link itself is the secret
    HTTPClient http;
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);  // iCloud bounces to its pNN- host
    http.setUserAgent("lr-clock");
    http.useHTTP10(true);  // no chunked encoding -> simple read loop below
    if (!http.begin(tls, httpsUrl(raw))) {
      status_ = "bad link";
      return false;
    }
    int code = http.GET();
    if (code != 200) {
      status_ = "HTTP " + String(code) + (code < 0 ? " (" + http.errorToString(code) + ")" : String(""));
      http.end();
      return false;
    }
    String err;
    bool ok = readBody(http, reader, &err);
    http.end();
    if (!ok) {
      status_ = err;
      return false;
    }
    reader.feed("\n", 1);  // keep two calendars from gluing their last/first lines
    ++fetched;
  }

  std::vector<CalEvent> events = reader.finish();
  cal.setEvents(events);
  char buf[120];
  snprintf(buf, sizeof(buf), "ok %02d:%02d - %d calendar(s), %u KB, %u events read, %u today/tomorrow, %lu ms",
           t.tm_hour, t.tm_min, fetched, (unsigned)(reader.bytes() / 1024), (unsigned)reader.veventCount(),
           (unsigned)events.size(), (unsigned long)(millis() - t0));
  status_ = buf;
  Serial.printf("[cal] %s\n", buf);
  return true;
}
