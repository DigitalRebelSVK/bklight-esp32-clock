// ============================================================================
//  Copy this file to src/clock_config.h and fill it in. clock_clock_config.h is git-ignored,
//  so your Wi-Fi password, token and calendar link never end up in a commit.
//  Anything not set here gets a default from config_defaults.h.
// ============================================================================
#pragma once

// ---- Wi-Fi / network ---------------------------------------------------------
#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"
#define HOSTNAME "clock"  // -> http://clock.local

// Shared secret for the POST endpoints (/msg, /calendar, /brightness): send it
// as header "X-Token" or as a form/JSON field "token". Also the default
// password for Wi-Fi updates. Pick a long random string.
#define API_TOKEN "change-me"
// #define OTA_PASSWORD "..."   // separate password for Wi-Fi updates (user "admin")

// ---- Time --------------------------------------------------------------------
// POSIX TZ string. This one is Central Europe with automatic summer time.
#define TZ_POSIX "CET-1CEST,M3.5.0,M10.5.0/3"
#define NTP_1 "pool.ntp.org"
#define NTP_2 "time.cloudflare.com"

// ---- Weather (Open-Meteo, no API key) ------------------------------------------
#define LATITUDE 48.1486f   // Bratislava - put your own
#define LONGITUDE 17.1077f
#define WEATHER_REFRESH_MIN 15
#define WEATHER_STALE_MIN 120  // after this, show "--" instead of old data

// ---- Calendar ----------------------------------------------------------------
// Public .ics / webcal:// link (iCloud "Public Calendar", Google "Secret address
// in iCal format", Nextcloud, ...). Leave "" to use only the /calendar endpoint.
// The link is a secret: anyone with it can read the calendar.
#define CAL_ICS_URL ""
// #define CAL_ICS_URL2 ""  // optional second calendar, merged with the first

#define LBL_TODAY "Dnes"            // "Today"
#define LBL_TOMORROW "Zajtra"       // "Tomorrow"
#define EVENT_GRACE_MIN 15          // keep showing an event this long after it starts
#define SHOW_TOMORROW_FROM_HOUR 18  // tomorrow's events appear from this hour
#define EVENTS_SHOWN 2              // how many upcoming events rotate in
#define UPPERCASE_TEXT 1            // caps read much better on 8 px lines

// ---- Page rotation -------------------------------------------------------------
// Every ROTATE_PERIOD_S the next events are shown for EVENT_SHOW_S each, starting
// at second ROTATE_OFFSET_S, so the clock is always on screen at the minute change.
#define ROTATE_PERIOD_S 60
#define ROTATE_OFFSET_S 30
#define EVENT_SHOW_S 5
#define MSG_DEFAULT_S 120   // /msg display time if "sec" is not given
#define MSG_PAGE_S 3        // long messages flip pages this often

// ---- Panel -----------------------------------------------------------------
// 64x16 (ACT1025) or 32x32 (ACT1026). PlatformIO sets these per environment.
#ifndef PANEL_W
#define PANEL_W 64
#endif
#ifndef PANEL_H
#define PANEL_H 16
#endif

// Panel MAC as shown by a BLE scanner. Leave empty ("") to connect to the
// first device advertising as LED_BLE_*.
#define PANEL_MAC ""
#define PANEL_NAME_PREFIX "LED_BLE_"

// 0 = dim by scaling the pixel colours (works well, recommended), 1 = the
// panel's own brightness command 05 00 04 80 <0..100> (from the iPixel
// protocol family, not confirmed on BK-Light). The settings page can change it.
#define USE_HW_BRIGHTNESS 0

// Last byte of the frame header. Bk-Light-AppBypass uses 0x65. pypixelcolor
// uses 0x00 = "show only, don't save". See README, "power-cycle test".
#define FRAME_SLOT_BYTE 0x65

// Max bytes per BLE write (also capped at MTU-3). Frames are ~150-300 B, so
// with the negotiated 512 MTU they go out in one write. If frames are not
// ACKed, try 244.
#define BLE_CHUNK 509

// ---- Look -------------------------------------------------------------------
// Default clock font until changed on http://clock.local/ :
// 0 Segment, 1 Rounded, 2 Slim, 3 Dots
#define CLOCK_FONT 2

// ---- Light sensor -------------------------------------------------------------
// 0 = none, 1 = VEML7700 on I2C, 2 = phototransistor (e.g. 3DU33) on the ADC.
// See README for wiring. With 0, brightness comes from the settings page.
#define LIGHT_SENSOR 0
#define PT_ADC_PIN 34    // phototransistor: ADC1 pin (classic ESP32)
#define PT_RANGE_PIN 27  // phototransistor: range-switch pin
#define I2C_SDA 21       // VEML7700: classic ESP32 21/22, ESP32-S3 DevKitC 8/9
#define I2C_SCL 22

// ---- Indoor thermometer (optional) ---------------------------------------------
// 100k NTC: 3V3 -- 100k -- NTC_PIN -- NTC -- GND, plus 100 nF from NTC_PIN to
// GND. See README for the schematic. -1 = none. On a classic ESP32, GPIO35 is
// a free ADC1 pin (ADC2 pins don't work while Wi-Fi is on).
#define NTC_PIN -1
#define NTC_R_SERIES 100000.0f  // the fixed resistor
#define NTC_R25 100000.0f       // NTC at 25 degC
#define NTC_BETA 3950.0f        // B value from the NTC's listing/datasheet

#define LUX_DIM 1.0f        // <= this: minimum brightness
#define LUX_BRIGHT 300.0f   // >= this: maximum brightness
#define BRIGHT_MIN 4        // panel units 0..100
#define BRIGHT_MAX 100
#define NIGHT_ON_LUX 0.8f   // red clock-only night mode below this...
#define NIGHT_OFF_LUX 3.0f  // ...until it gets brighter than this

// Defaults for the settings page. Once you change something there, the saved
// values win over these. Without a sensor: fixed brightness and a night window.
#define BRIGHT_FIXED 40
#define NIGHT_START_H 23
#define NIGHT_END_H 6
