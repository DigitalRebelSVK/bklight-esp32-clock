#include "light_sensor.h"

#include <Wire.h>

#include "config_defaults.h"

// ---- VEML7700 -------------------------------------------------------------------
namespace {
constexpr uint8_t kAddr = 0x10;
constexpr uint8_t kRegConf = 0x00;
constexpr uint8_t kRegAls = 0x04;

// Gain bits 12:11, integration time bits 9:6 (VEML7700 datasheet).
//   sensitive: gain x2,   IT 400 ms -> 0.0084 lx/count, saturates ~550 lx
//   bright:    gain x1/8, IT 100 ms -> 0.5376 lx/count, saturates ~35 klx
constexpr uint16_t kConfSensitive = (0b01 << 11) | (0b0010 << 6);
constexpr uint16_t kConfBright = (0b10 << 11) | (0b0000 << 6);
constexpr float kResSensitive = 0.0084f;
constexpr float kResBright = 0.5376f;
constexpr uint32_t kItSensitiveMs = 400;
constexpr uint32_t kItBrightMs = 100;

// ---- phototransistor ------------------------------------------------------------
// Two ranges so the ESP32 ADC never has to read in its dead zone (< ~0.1 V):
//   dark:   100k load          -> 0.1-2.5 V = 1-25 uA   (~1.3-33 lx at 0.75 uA/lx)
//   bright: 100k || 10k = 9.1k -> 0.1-2.5 V = 11-275 uA (~15-370 lx)
// Up-switch at 2.5 V (25 uA) lands at 0.23 V in the bright range; the
// down-switch at 0.15 V (16.5 uA) lands at 1.65 V in the dark range, so the
// ranges overlap with plenty of hysteresis.
constexpr float kPtUpMv = 2500.0f;
constexpr float kPtDownMv = 150.0f;
constexpr int kPtSamples = 20;  // 20 x 1 ms = two periods of 100 Hz lamp flicker
}  // namespace

bool LightSensor::begin() {
#if LIGHT_SENSOR == 1
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  if (vemlConfig(true)) kind_ = kVeml;
#elif LIGHT_SENSOR == 2
  pinMode(PT_ADC_PIN, INPUT);
  analogReadResolution(12);
  photoRange(true);
  kind_ = kPhoto;
#endif
  return present();
}

const char* LightSensor::name() const {
  switch (kind_) {
    case kVeml: return "VEML7700";
    case kPhoto: return "phototransistor";
    default: return "none";
  }
}

bool LightSensor::poll(float* lux) {
  if (kind_ == kVeml) return vemlPoll(lux);
  if (kind_ == kPhoto) return photoPoll(lux);
  return false;
}

// ---- VEML7700 -------------------------------------------------------------------
bool LightSensor::vemlConfig(bool sensitive) {
  uint16_t v = sensitive ? kConfSensitive : kConfBright;  // ALS_SD = 0 -> running
  Wire.beginTransmission(kAddr);
  Wire.write(kRegConf);
  Wire.write(v & 0xFF);
  Wire.write(v >> 8);
  if (Wire.endTransmission() != 0) return false;
  sensitive_ = sensitive;
  // Throw away the integration that was running when the range changed.
  settleUntil_ = millis() + 2 * (sensitive ? kItSensitiveMs : kItBrightMs) + 20;
  return true;
}

bool LightSensor::vemlRead(uint16_t* raw) {
  Wire.beginTransmission(kAddr);
  Wire.write(kRegAls);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)kAddr, 2) != 2) return false;
  uint8_t lo = Wire.read();
  uint8_t hi = Wire.read();
  *raw = (uint16_t)(hi << 8 | lo);
  return true;
}

bool LightSensor::vemlPoll(float* lux) {
  uint32_t now = millis();
  if ((int32_t)(now - settleUntil_) < 0 || (int32_t)(now - nextRead_) < 0) return false;
  nextRead_ = now + (sensitive_ ? kItSensitiveMs : kItBrightMs) + 10;
  uint16_t raw;
  if (!vemlRead(&raw)) return false;
  raw_ = raw;
  if (sensitive_ && raw > 50000) {  // ~420 lx: switch to the daylight range
    vemlConfig(false);
    return false;
  }
  if (!sensitive_ && raw < 400) {  // ~215 lx: back to the night range
    vemlConfig(true);
    return false;
  }
  *lux = raw * (sensitive_ ? kResSensitive : kResBright);
  return true;
}

// ---- phototransistor --------------------------------------------------------------
void LightSensor::photoRange(bool dark) {
  sensitive_ = dark;
  if (dark) {
    pinMode(PT_RANGE_PIN, INPUT);  // high-Z: only the 100k load
  } else {
    pinMode(PT_RANGE_PIN, OUTPUT);  // 10k to GND in parallel
    digitalWrite(PT_RANGE_PIN, LOW);
  }
  // 1 uF with 100k is a 100 ms time constant; give it ~4 of them.
  settleUntil_ = millis() + (dark ? 400 : 60);
}

bool LightSensor::photoPoll(float* lux) {
  uint32_t now = millis();
  if ((int32_t)(now - settleUntil_) < 0 || (int32_t)(now - nextRead_) < 0) return false;
  nextRead_ = now + 500;

  uint32_t sum = 0;
  for (int i = 0; i < kPtSamples; ++i) {
    sum += analogReadMilliVolts(PT_ADC_PIN);
    delayMicroseconds(1000);
  }
  float mv = (float)sum / kPtSamples;
  raw_ = mv;

  if (sensitive_ && mv > kPtUpMv) {
    photoRange(false);
    return false;
  }
  if (!sensitive_ && mv < kPtDownMv) {
    photoRange(true);
    return false;
  }
  float r = sensitive_ ? PT_R_DARK : (PT_R_DARK * PT_R_RANGE) / (PT_R_DARK + PT_R_RANGE);
  microamps_ = mv / r * 1000.0f;  // mV / ohm = mA
  float l = microamps_ / PT_UA_PER_LUX;
  *lux = l < 0 ? 0 : l;
  return true;
}
