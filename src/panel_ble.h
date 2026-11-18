// BLE link to the BK-Light panel (NimBLE-Arduino 2.x).
#pragma once
#include <Arduino.h>

class NimBLEClient;
class NimBLERemoteCharacteristic;

struct PanelStats {
  uint32_t connects = 0;       // successful connects since boot
  uint32_t drops = 0;          // disconnects after a successful connect
  uint32_t missedAcks = 0;     // frames the panel did not acknowledge
  int lastReason = 0;          // NimBLE reason code of the last drop
  String lastCause;            // human-readable cause of the last drop
  uint32_t lastDropMs = 0;     // millis() of the last drop
  uint32_t lastReconnectMs = 0;  // how long the last reconnect took
};

class PanelBLE {
 public:
  void begin(const char* mac, const char* namePrefix);

  bool connected() const;
  // Reconnects straight to the last known address when there is one (~0.5 s),
  // otherwise scans first (~4 s). Blocking.
  bool connect();
  void disconnect(const char* cause);

  // Sends one wrapped PNG frame and waits for the panel's ACK.
  bool sendFrame(const uint8_t* frame, size_t len);
  // 0..100, iPixel-family command 05 00 04 80 <level>.
  bool setBrightness(uint8_t level);

  const String& lastError() const { return err_; }
  const String& lastNotify() const { return lastNotify_; }
  uint32_t framesSent() const { return frames_; }
  const PanelStats& stats() const { return stats_; }

  // Called from the NimBLE task when the link goes down.
  void onLinkLost(int reason);

 private:
  struct Notif {
    uint8_t len;
    uint8_t d[23];
  };
  bool write(const uint8_t* d, size_t n, bool response);
  bool waitFor(bool (*match)(const Notif&), uint32_t timeoutMs);
  void flushNotifs();
  bool handshake();
  bool attach();  // services, characteristics, notifications, handshake

  String mac_;
  String prefix_;
  String err_;
  String lastNotify_;
  String pendingCause_;
  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* wr_ = nullptr;
  NimBLERemoteCharacteristic* nt_ = nullptr;
  void* queue_ = nullptr;  // FreeRTOS QueueHandle_t
  void* addr_ = nullptr;   // NimBLEAddress* of the panel once found
  uint32_t frames_ = 0;
  uint32_t sinceValidation_ = 0;
  uint32_t missedInARow_ = 0;
  uint32_t lostAtMs_ = 0;
  volatile bool linkUp_ = false;
  PanelStats stats_;
};

// Short description of a NimBLE disconnect reason.
String bleReasonText(int reason);
