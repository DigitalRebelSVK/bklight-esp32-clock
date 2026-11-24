// Living-room clock: ESP32 -> BK-Light BLE LED panel.
// Clock (NTP) + Open-Meteo weather + .ics calendar + indoor NTC thermometer,
// brightness / night mode / clock font editable on http://clock.local/.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <sys/time.h>
#include <time.h>

#include "autobright.h"
#include "calendar.h"
#include "calendar_feed.h"
#include "config_defaults.h"
#include "fb.h"
#include "indoor_slide.h"
#include "indoor_temp.h"
#include "light_sensor.h"
#include "ota.h"
#include "panel_ble.h"
#include "png.h"
#include "render.h"
#include "settings.h"
#include "text.h"
#include "weather.h"
#include "web_ui.h"

namespace {

PanelBLE panel;
Weather weather;
LightSensor sensor;
IndoorTemp indoor;
Calendar calendar;
CalendarFeed calendarFeed;
WebServer server(80);
Framebuffer fb(PANEL_W, PANEL_H);
Settings settings;

AutoBright makeAutoBright() {
  AutoBrightConfig c;
  c.luxDim = LUX_DIM;
  c.luxBright = LUX_BRIGHT;
  c.minLevel = 0;  // 0..100 here, mapped onto autoMin..autoMax from the settings page
  c.maxLevel = 100;
  c.nightOnLux = NIGHT_ON_LUX;
  c.nightOffLux = NIGHT_OFF_LUX;
  return AutoBright(c);
}
AutoBright autoBright = makeAutoBright();

uint8_t pngBuf[6144];
uint8_t frameBuf[6144 + 16];

// State
String message;
RGB messageColor = {0, 210, 200};
uint32_t messageUntil = 0;
uint32_t messageStart = 0;
float lastLux = -1;
uint32_t lastLuxMs = 0;

uint32_t lastPixelsCrc = 0;
bool havePixels = false;
uint32_t lastSendMs = 0;
uint32_t nextPanelTry = 0;
uint32_t panelBackoff = 5000;
int appliedLevel = -1;
int appliedHw = -1;
size_t lastFrameLen = 0;
bool lastNight = false;
int lastLevel = 0;

bool settingsDirty = false;
uint32_t settingsChangedMs = 0;

// ---------------------------------------------------------------------------
bool timeValid(struct tm* t) {
  time_t now = time(nullptr);
  localtime_r(&now, t);
  return t->tm_year + 1900 >= 2024;
}

bool sensorReady() { return sensor.present() && autoBright.ready(); }

bool inWindow(int h, int from, int to) {
  if (from == to) return false;
  return from > to ? (h >= from || h < to) : (h >= from && h < to);
}

bool isNight(const struct tm& t, bool timeOk) {
  switch (settings.nightMode) {
    case 2:
      if (sensorReady()) return autoBright.night();
      // no sensor: fall back to the schedule
      return timeOk && inWindow(t.tm_hour, settings.nightStart, settings.nightEnd);
    case 1:
      return timeOk && inWindow(t.tm_hour, settings.nightStart, settings.nightEnd);
    default:
      return false;
  }
}

// Slider level 0..100 -> pixel brightness factor PIXEL_DIM_MIN..MAX %.
float pixelFactor(int level) {
  return (PIXEL_DIM_MIN + (PIXEL_DIM_MAX - PIXEL_DIM_MIN) * level / 100.0f) / 100.0f;
}

int wantedLevel(bool night) {
  if (night) return settings.nightLevel;
  if (settings.brightMode == 0 && sensorReady()) {
    int lo = settings.autoMin, hi = settings.autoMax;
    return lo + (int)lroundf(autoBright.level() * (hi - lo) / 100.0f);
  }
  return settings.dayLevel;
}

// Reads `field` from form/query args or a JSON body. With rawFallback, a plain
// text body (no form, no JSON) is returned as the value.
String bodyField(const char* field, bool rawFallback = false) {
  if (server.hasArg(field)) return server.arg(field);
  if (!server.hasArg("plain")) return "";
  String raw = server.arg("plain");
  String trimmed = raw;
  trimmed.trim();
  if (trimmed.startsWith("{")) {
    JsonDocument doc;
    if (deserializeJson(doc, raw) || doc[field].isNull()) return "";
    return doc[field].as<String>();
  }
  return rawFallback ? raw : String();
}

bool authorized() {
  if (strlen(API_TOKEN) == 0) return true;
  if (server.header("X-Token") == API_TOKEN) return true;
  return bodyField("token") == API_TOKEN;
}

bool parseColor(const String& hex, RGB* out) {
  String h = hex;
  h.replace("#", "");
  if (h.length() != 6) return false;
  long v = strtol(h.c_str(), nullptr, 16);
  *out = {(uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
  return true;
}

void markSettingsChanged() {
  settingsDirty = true;
  settingsChangedMs = millis();
}

// Indoor temperature as displayed (offset from the settings page applied).
bool indoorValid() { return indoor.valid(); }
int indoorTenths() { return indoor.shownTenths((int)settings.indoorOffset - 50); }

// Where the once-a-minute indoor slide is right now. Daytime only, and only
// when both temperatures are there to slide between.
SlideState currentSlide(bool night) {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  struct tm t;
  localtime_r(&tv.tv_sec, &t);
  bool on = settings.indoorSlide && !night && t.tm_year + 1900 >= 2024 && indoorValid() && weather.valid();
  return slideAt(t.tm_sec * 1000 + (int)(tv.tv_usec / 1000), on, INDOOR_SLIDE_MS, INDOOR_HOLD_S * 1000);
}

// Downloads block the loop for 1-3 s; keep them out of the animation window
// and the last seconds before it, so the slide never freezes halfway.
bool networkQuietTime(bool night) {
  if (!indoor.enabled() || !settings.indoorSlide || night) return true;
  time_t now = time(nullptr);
  struct tm t;
  localtime_r(&now, &t);
  int sec = t.tm_sec;
  return !(sec >= 56 || sec < (2 * INDOOR_SLIDE_MS + INDOOR_HOLD_S * 1000) / 1000 + 2);
}

// ---- web: settings page + JSON API --------------------------------------------
String uptimeText() {
  uint32_t s = millis() / 1000;
  char b[24];
  if (s >= 86400) snprintf(b, sizeof(b), "%lud %luh", (unsigned long)(s / 86400), (unsigned long)(s % 86400 / 3600));
  else snprintf(b, sizeof(b), "%luh %lum", (unsigned long)(s / 3600), (unsigned long)(s % 3600 / 60));
  return b;
}

void sendState() {
  JsonDocument doc;
  JsonObject so = doc["settings"].to<JsonObject>();
  for (int i = 0; i < kSettingCount; ++i) so[kSettingKeys[i]] = settingsGet(settings, kSettingKeys[i]);

  JsonObject st = doc["status"].to<JsonObject>();
  struct tm t;
  if (timeValid(&t)) {
    char ts[8];
    strftime(ts, sizeof(ts), "%H:%M", &t);
    st["time"] = ts;
  }
  if (weather.valid()) {
    int tenths = (int)lroundf(weather.tempExact() * 10);
    st["temp"] = tempLabel(tenths, abs(tenths) < 100);
  }
  st["ntc"] = indoor.enabled();
  if (indoorValid()) {
    st["indoor"] = tempLabel(indoorTenths(), true);
    st["indoorRaw"] = serialized(String(indoor.rawCelsius(), 2));
  } else if (indoor.enabled()) {
    st["indoorFault"] = indoor.fault();
  }
  st["panel"] = panel.connected();
  st["frameMs"] = panel.stats().frameMsAvg;
  st["sensor"] = sensor.present();
  st["lux"] = serialized(String(autoBright.smoothedLux(), 1));
  st["level"] = lastLevel;
  st["pixelPct"] = (int)lroundf(pixelFactor(lastLevel) * 100);
  st["night"] = lastNight;
  st["sensorName"] = sensor.name();
  st["sensorRaw"] = serialized(String(sensor.raw(), 0));
  const PanelStats& ps = panel.stats();
  st["drops"] = ps.drops;
  st["lastDrop"] = ps.lastCause;
  st["lastDropAgo"] = ps.lastDropMs ? (millis() - ps.lastDropMs) / 1000 : 0;
  st["missedAcks"] = ps.missedAcks;
  st["calendar"] = calendarFeed.enabled() ? calendarFeed.status() : String("push only");
  st["events"] = (int)calendar.size();
  st["ip"] = WiFi.localIP().toString();
  st["uptime"] = uptimeText();
  st["fw"] = ota::firmwareId();
  st["fwInfo"] = ota::status();

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleIndex() { server.send(200, "text/html; charset=utf-8", kIndexHtml); }

// Settings only change how the clock looks, so they are open on the LAN; the
// endpoints that put content on the screen (/calendar, /msg) keep the token.
void handleSettingsPost() {
  bool changed = false;
  for (int i = 0; i < kSettingCount; ++i) {
    String v = bodyField(kSettingKeys[i]);
    if (v.length() == 0) continue;
    if (settingsSet(settings, kSettingKeys[i], v.toInt())) changed = true;
  }
  if (settings.autoMin > settings.autoMax) settings.autoMax = settings.autoMin;
  if (changed) markSettingsChanged();
  sendState();
}

void handleCalendar() {
  if (!authorized()) return server.send(401, "text/plain", "bad token\n");
  String body = bodyField("events", true);
  int n = calendar.setFromText(std::string(body.c_str()));
  Serial.printf("[web] calendar: %d events\n", n);
  server.send(200, "text/plain", "ok " + String(n) + " events\n");
}

void handleMessage() {
  if (!authorized()) return server.send(401, "text/plain", "bad token\n");
  String txt = bodyField("text", true);
  txt.trim();
  if (txt.length() == 0) {
    messageUntil = 0;
    return server.send(200, "text/plain", "cleared\n");
  }
  int sec = bodyField("sec").toInt();
  String col = bodyField("color");
  if (sec <= 0) sec = MSG_DEFAULT_S;
  messageColor = {0, 210, 200};
  if (col.length()) parseColor(col, &messageColor);
  message = txt;
  messageStart = millis();
  messageUntil = millis() + (uint32_t)sec * 1000UL;
  server.send(200, "text/plain", "showing for " + String(sec) + " s" +
                                    (sec > MSG_TAKEOVER_S ? String(" (rotating with the clock)") : String("")) + "\n");
}

// Kept for automations: level=auto -> sensor mode, level=N -> manual N.
void handleBrightness() {
  if (!authorized()) return server.send(401, "text/plain", "bad token\n");
  String lv = bodyField("level");
  if (lv == "auto" || lv.length() == 0) {
    settings.brightMode = 0;
  } else {
    settings.brightMode = 1;
    settings.dayLevel = (uint8_t)constrain(lv.toInt(), 0, 100);
  }
  markSettingsChanged();
  server.send(200, "text/plain", settings.brightMode == 0 ? "auto\n" : "manual " + String(settings.dayLevel) + "\n");
}

void handleStatusText() {
  struct tm t;
  bool tv = timeValid(&t);
  char ts[32];
  strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &t);
  String s;
  s += "living-room clock " + String(PANEL_W) + "x" + String(PANEL_H) + "\n";
  s += "time:       " + String(tv ? ts : "not synced") + "\n";
  s += "wifi:       " + WiFi.localIP().toString() + "  RSSI " + String(WiFi.RSSI()) + " dBm\n";
  s += "panel:      " + String(panel.connected() ? "connected" : "disconnected");
  if (panel.lastError().length()) s += "  (last error: " + panel.lastError() + ")";
  const PanelStats& ps = panel.stats();
  s += "\n            frames sent " + String(panel.framesSent()) + ", last frame " + String(lastFrameLen) +
       " B, write+ACK " + String(ps.frameMsAvg) + " ms avg / " + String(ps.frameMsMax) + " ms max, last notify " +
       panel.lastNotify() + "\n";
  s += "            connects " + String(ps.connects) + ", drops " + String(ps.drops) + ", missed ACKs " +
       String(ps.missedAcks) + "\n";
  if (ps.drops)
    s += "            last drop " + String((millis() - ps.lastDropMs) / 1000) + " s ago: " + ps.lastCause +
         " (reason " + String(ps.lastReason) + "), reconnect took " + String(ps.lastReconnectMs) + " ms\n";
  s += "light:      " + String(sensor.name());
  if (sensor.present()) {
    s += "  " + String(autoBright.smoothedLux(), 2) + " lx (now " + String(lastLux, 2) + " lx, raw " +
         String(sensor.raw(), 1) + (LIGHT_SENSOR == 2 ? " mV, " + String(sensor.microamps(), 2) + " uA, " +
                                        sensor.range() + " range" : String(" counts")) + ")";
  }
  s += "\nbrightness: " + String(lastLevel) + (settings.brightMode ? " (manual)" : " (auto)") +
       (lastNight ? "  NIGHT" : "") +
       (settings.hwBrightness ? String("  [panel command]")
                              : "  [pixel dimming " + String((int)lroundf(pixelFactor(lastLevel) * 100)) + " %]") +
       "\n";
  s += "indoor:     ";
  if (!indoor.enabled()) {
    s += "no thermometer (NTC_PIN -1)\n";
  } else if (!indoorValid()) {
    s += String(*indoor.fault() ? indoor.fault() : "waiting for first reading") + "  (" +
         String(indoor.millivolts(), 0) + " mV)\n";
  } else {
    int off = (int)settings.indoorOffset - 50;
    s += String(tempLabel(indoorTenths(), true).c_str()) + " C  (sensor " + String(indoor.rawCelsius(), 2) +
         " C, offset " + String(off / 10.0f, 1) + ", " + String(indoor.millivolts(), 0) + " mV, " +
         String(indoor.ohms() / 1000.0f, 1) + " kOhm)" + (settings.indoorSlide ? "  [slide every minute]" : "") +
         "\n";
  }
  s += "font:       " + String(clockFontName(settings.clockFont)) + "\n";
  s += "weather:    " + weather.status() + (weather.valid() ? "" : "  [stale]") + "\n";
  s += "calendar:   " + String(calendar.size()) + " stored" +
       (calendarFeed.enabled() ? "  [link: " + calendarFeed.status() + "]" : String("  [push only]")) + "\n";
  for (const auto& e : calendar.all()) {
    char hm[16] = "*    ";
    if (e.minute >= 0) snprintf(hm, sizeof(hm), "%02d:%02d", e.minute / 60, e.minute % 60);
    char line[120];
    snprintf(line, sizeof(line), "            %04d-%02d-%02d %s %s\n", e.y, e.m, e.d, hm, e.title.c_str());
    s += line;
  }
  s += "message:    " + (millis() < messageUntil ? message : String("-")) + "\n";
  s += "firmware:   " + ota::status() + "\n";
  s += "heap:       " + String(ESP.getFreeHeap()) + " B free, largest block " + String(ESP.getMaxAllocHeap()) +
       " B, lowest ever " + String(ESP.getMinFreeHeap()) + " B\n";
  server.send(200, "text/plain; charset=utf-8", s);
}

// ---------------------------------------------------------------------------
void maintainWifi() {
  static uint32_t lastAttempt = 0;
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastAttempt < 15000) return;
  lastAttempt = millis();
  Serial.println("[wifi] reconnecting");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

void updateLight() {
  float lux;
  if (sensor.poll(&lux)) {
    uint32_t now = millis();
    float dt = lastLuxMs ? (now - lastLuxMs) / 1000.0f : 1.0f;
    lastLuxMs = now;
    lastLux = lux;
    autoBright.update(lux, dt);
  }
}

std::string maybeUpper(const std::string& s) { return UPPERCASE_TEXT ? text::upper(s) : s; }

Screen buildScreen(bool night) {
  Screen s;
  struct tm t;
  s.timeValid = timeValid(&t);
  s.hh = t.tm_hour;
  s.mm = t.tm_min;
  s.clockFont = settings.clockFont;
  s.night = night && settings.nightRed;  // red clock-only page
  s.wxValid = weather.valid();
  s.tempTenths = (int)lroundf(weather.tempExact() * 10);
  s.icon = iconForWmo(weather.code(), weather.isDay());
  s.inValid = indoorValid();
  if (s.inValid) s.inTenths = indoorTenths();
  if (!s.wxValid && s.inValid) {
    s.wxFront = Screen::kIndoor;  // no weather yet: show what we have
  } else {
    SlideState sl = currentSlide(night);
    s.wxFront = sl.front;
    s.wxSlide = sl.slide;
  }

  uint32_t now = millis();
  bool msgActive = messageUntil && (int32_t)(messageUntil - now) > 0;
  bool msgLong = msgActive && messageUntil - messageStart > (uint32_t)MSG_TAKEOVER_S * 1000UL;
  bool msgSlot = msgActive && (!msgLong || ((now - messageStart) / 1000UL) % MSG_ROTATE_PERIOD_S < MSG_ROTATE_SHOW_S);
  if (msgSlot) {
    s.page = Screen::kMessage;
    s.msg = maybeUpper(message.c_str());
    s.msgColor = messageColor;
    int pages = messagePages(s.msg, PANEL_W, PANEL_H);
    s.msgPage = (int)(((now - messageStart) / 1000UL / MSG_PAGE_S) % pages);
    return s;
  }
  if (!s.timeValid) return s;

  Today today{t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour * 60 + t.tm_min};
  std::vector<CalEvent> up = calendar.upcoming(today, EVENT_GRACE_MIN, SHOW_TOMORROW_FROM_HOUR);
  if (!up.empty()) {
    s.nextLabel = maybeUpper(Calendar::label(up[0], today, LBL_TODAY, LBL_TOMORROW));
    if (text::length(s.nextLabel) > 6) s.nextLabel.clear();  // 32x32 hint fits 6 chars
  }
  if (s.night || up.empty()) return s;

  int shown = (int)up.size() < EVENTS_SHOWN ? (int)up.size() : EVENTS_SHOWN;
  long epoch = (long)time(nullptr);
  int phase = (int)(((epoch - ROTATE_OFFSET_S) % ROTATE_PERIOD_S + ROTATE_PERIOD_S) % ROTATE_PERIOD_S);
  if (phase < shown * EVENT_SHOW_S) {
    const CalEvent& e = up[phase / EVENT_SHOW_S];
    s.page = Screen::kEvent;
    s.evLabel = maybeUpper(Calendar::label(e, today, LBL_TODAY, LBL_TOMORROW));
    s.evTitle = maybeUpper(e.title);
  }
  return s;
}

void pushToPanel() {
  if ((int32_t)(millis() - nextPanelTry) < 0) return;

  struct tm t;
  bool tv = timeValid(&t);
  bool night = isNight(t, tv);
  int level = wantedLevel(night);
  bool hw = settings.hwBrightness != 0;
  lastNight = night;
  lastLevel = level;

  renderScreen(buildScreen(night), fb);
  if (!hw) fb.scale(pixelFactor(level));
  uint32_t crc = png::crc32(fb.data(), fb.bytes());

  bool pixelsChanged = !havePixels || crc != lastPixelsCrc;
  bool refresh = millis() - lastSendMs > 10UL * 60000UL;  // re-send every 10 min anyway
  int hwTarget = hw ? level : 100;  // pixel dimming runs with the panel at full
  bool levelChanged = hwTarget != appliedLevel || (int)hw != appliedHw;
  if (!pixelsChanged && !refresh && !levelChanged && panel.connected()) return;

  if (!panel.connected()) {
    if (!panel.connect()) {
      // Scanning blocks the web server for a few seconds, so back off up to 1 min.
      Serial.printf("[ble] %s (retry in %lu s)\n", panel.lastError().c_str(), (unsigned long)(panelBackoff / 1000));
      nextPanelTry = millis() + panelBackoff;
      panelBackoff = panelBackoff * 2 > 60000 ? 60000 : panelBackoff * 2;
      return;
    }
    panelBackoff = 5000;
    havePixels = false;  // panel may show something else after reconnect
    appliedLevel = -1;
  }

  if (levelChanged && panel.setBrightness((uint8_t)hwTarget)) {
    appliedLevel = hwTarget;
    appliedHw = hw;
  }

  if (!havePixels || crc != lastPixelsCrc || refresh) {
    size_t n = png::encode(fb.data(), fb.width(), fb.height(), pngBuf, sizeof(pngBuf));
    size_t f = n ? bkframe::build(pngBuf, n, FRAME_SLOT_BYTE, frameBuf, sizeof(frameBuf)) : 0;
    if (!f) {
      Serial.println("[png] encode failed");
      return;
    }
    if (panel.sendFrame(frameBuf, f)) {
      lastPixelsCrc = crc;
      havePixels = true;
      lastSendMs = millis();
      lastFrameLen = f;
    } else {
      Serial.printf("[ble] send failed: %s\n", panel.lastError().c_str());
      nextPanelTry = millis() + 5000;
    }
  }
}

// Firmware-update notices ("FIRMWARE UPDATE", "RESTART"). The upload blocks the
// loop, so the frame is sent right away; skipped if the panel is not linked,
// because a reconnect scan would stall the upload handshake.
void showSystemText(const char* text, int seconds) {
  message = text;
  messageColor = {255, 150, 0};
  messageStart = millis();
  messageUntil = messageStart + (uint32_t)seconds * 1000UL;
  if (!panel.connected()) return;
  nextPanelTry = 0;
  pushToPanel();
}

// Slider drags produce many changes; write flash once things settle.
void saveSettingsIfIdle() {
  if (settingsDirty && millis() - settingsChangedMs > 3000) {
    settingsSave(settings);
    settingsDirty = false;
    Serial.println("[cfg] settings saved");
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nliving-room clock starting");

  settingsLoad(settings);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTzTime(TZ_POSIX, NTP_1, NTP_2);

  if (sensor.begin()) Serial.printf("[light] %s\n", sensor.name());
  else Serial.println("[light] no sensor - brightness from the settings page");
  if (indoor.begin()) Serial.printf("[indoor] NTC on GPIO%d\n", NTC_PIN);

  panel.begin(PANEL_MAC, PANEL_NAME_PREFIX);

  const char* headers[] = {"X-Token"};
  server.collectHeaders(headers, 1);
  server.on("/", HTTP_GET, handleIndex);
  server.on("/api/state", HTTP_GET, sendState);
  server.on("/api/settings", HTTP_POST, handleSettingsPost);
  server.on("/status.txt", HTTP_GET, handleStatusText);
  server.on("/calendar", HTTP_POST, handleCalendar);
  server.on("/msg", HTTP_POST, handleMessage);
  server.on("/brightness", HTTP_POST, handleBrightness);
  ota::begin(server, showSystemText);
  server.begin();
}

void loop() {
  static bool mdnsUp = false;
  maintainWifi();
  bool wifiUp = WiFi.status() == WL_CONNECTED;
  if (wifiUp && !mdnsUp) {
    mdnsUp = MDNS.begin(HOSTNAME);
    if (mdnsUp) {
      MDNS.addService("http", "tcp", 80);
      ota::startNetwork();
    }
    Serial.printf("[wifi] %s, http://%s.local\n", WiFi.localIP().toString().c_str(), HOSTNAME);
  }
  server.handleClient();
  ota::loop(wifiUp);
  updateLight();
  indoor.poll();
  bool quiet = networkQuietTime(lastNight);
  weather.update(wifiUp && quiet);
  calendarFeed.update(wifiUp && quiet, calendar);
  pushToPanel();
  saveSettingsIfIdle();
  // While sliding, send the next frame as soon as the panel ACKs; otherwise idle
  // (a busy loop warms the ESP32, and with it a nearby NTC).
  delay(currentSlide(lastNight).slide > 0 ? 1 : 20);
}
