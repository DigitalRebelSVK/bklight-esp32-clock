// Defaults for settings added after the first release, so an older clock_config.h
// keeps compiling. Override any of these by #define-ing it in clock_config.h.
#pragma once
#if __has_include("clock_config.h")
#include "clock_config.h"
#elif !defined(ARDUINO)
#include "clock_config.example.h"  // host-side tests and previews only
#else
#error "Copy src/clock_config.example.h to src/clock_config.h and fill in your Wi-Fi, token and location."
#endif

// ---- Look -------------------------------------------------------------------
#ifndef CLOCK_FONT
#define CLOCK_FONT 2  // 0 Segment, 1 Rounded, 2 Slim, 3 Dots
#endif

// ---- Pixel dimming ------------------------------------------------------------
// The brightness slider (0..100) maps onto this range of pixel brightness in %.
// Below ~22 % colours band and thin strokes start to vanish, so 0 = 22 %.
#ifndef PIXEL_DIM_MIN
#define PIXEL_DIM_MIN 22
#endif
#ifndef PIXEL_DIM_MAX
#define PIXEL_DIM_MAX 100
#endif

// ---- Light sensor -------------------------------------------------------------
// 0 = none, 1 = VEML7700 (I2C), 2 = 3DU33 phototransistor (or any NPN one) on the ADC
#ifndef LIGHT_SENSOR
#define LIGHT_SENSOR 0
#endif
// Phototransistor wiring (see README):
//   3V3 -> C | E -> PT_ADC_PIN, with 100k + 1uF to GND, and 10k to PT_RANGE_PIN
#ifndef PT_ADC_PIN
#define PT_ADC_PIN 34    // ADC1 (ADC2 is unusable while Wi-Fi is on), input-only pin
#endif
#ifndef PT_RANGE_PIN
#define PT_RANGE_PIN 27  // LOW = bright range (10k in parallel), high-Z = dark range
#endif
#ifndef PT_R_DARK
#define PT_R_DARK 100000.0f  // ohms, always connected E -> GND
#endif
#ifndef PT_R_RANGE
#define PT_R_RANGE 10000.0f  // ohms, E -> PT_RANGE_PIN
#endif
// Calibration: photo current per lux. 3DU33 parts are sold as 0.5-1 mA at the
// test illumination, so ~0.75 uA/lx is a starting point; see README to tune it.
#ifndef PT_UA_PER_LUX
#define PT_UA_PER_LUX 0.75f
#endif

// ---- BLE link -----------------------------------------------------------------
// Bk-Light-AppBypass sends 05 00 00 01 00 every 30 frames. Its meaning is
// unknown and it is a suspect for the panel briefly showing its own screen,
// so it is off by default. Set 1 to restore the repo's behaviour.
#ifndef BLE_SEND_VALIDATION
#define BLE_SEND_VALIDATION 0
#endif
// How long the link may go silent before it counts as lost. Wi-Fi and BLE
// share one radio; a longer timeout rides out bursts of Wi-Fi traffic.
#ifndef BLE_SUPERVISION_MS
#define BLE_SUPERVISION_MS 6000
#endif

// ---- Firmware updates over Wi-Fi --------------------------------------------------
// Password for the Arduino IDE network port and for http://clock.local/update
// (user "admin"). Defaults to API_TOKEN; "" turns the password off.
#ifndef OTA_PASSWORD
#define OTA_PASSWORD API_TOKEN
#endif

// ---- Calendar from a public link ----------------------------------------------
// iCloud: Calendar -> share the calendar -> "Public Calendar" -> copy link
// (webcal://... is fine). Leave empty to use only the /calendar push endpoint.
#ifndef CAL_ICS_URL
#define CAL_ICS_URL ""
#endif
#ifndef CAL_ICS_URL2
#define CAL_ICS_URL2 ""  // optional second calendar, merged with the first
#endif
#ifndef CAL_REFRESH_MIN
#define CAL_REFRESH_MIN 15
#endif

// ---- Messages ----------------------------------------------------------------------
// Messages up to MSG_TAKEOVER_S seconds take over the screen. Longer ones
// (e.g. "washer done" for 30 min) rotate in: MSG_ROTATE_SHOW_S out of every
// MSG_ROTATE_PERIOD_S, so the clock stays readable.
#ifndef MSG_TAKEOVER_S
#define MSG_TAKEOVER_S 60
#endif
#ifndef MSG_ROTATE_PERIOD_S
#define MSG_ROTATE_PERIOD_S 20
#endif
#ifndef MSG_ROTATE_SHOW_S
#define MSG_ROTATE_SHOW_S 6
#endif
