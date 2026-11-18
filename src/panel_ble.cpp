#include "panel_ble.h"

#include <NimBLEDevice.h>
#include <time.h>

#include "config_defaults.h"

// Protocol notes (from Bk-Light-AppBypass + pypixelcolor):
//   write char 0xFA02, notify char 0xFA03
//   08 00 01 80 HH MM SS 00   set clock / hello  -> reply xx 00 01 80 ...
//   04 00 05 80               firmware version   -> reply xx 00 05 80 ... (optional)
//   <frame>                   PNG frame          -> reply 05 00 02 00 03
//   05 00 00 01 00            "validation" from the repo, meaning unknown (off by default)
//   05 00 04 80 LL            brightness 0..100

namespace {
const NimBLEUUID kWriteUuid((uint16_t)0xFA02);
const NimBLEUUID kNotifyUuid((uint16_t)0xFA03);
constexpr uint32_t kMaxMissedAcks = 3;  // drop the link only after this many in a row

class Callbacks : public NimBLEClientCallbacks {
 public:
  explicit Callbacks(PanelBLE* p) : panel_(p) {}
  void onDisconnect(NimBLEClient*, int reason) override { panel_->onLinkLost(reason); }

 private:
  PanelBLE* panel_;
};
}  // namespace

String bleReasonText(int reason) {
  switch (reason) {
    case 0: return "-";
    case BLE_HS_ERR_HCI_BASE + BLE_ERR_CONN_SPVN_TMO:
      return "supervision timeout (radio busy / out of range)";
    case BLE_HS_ERR_HCI_BASE + BLE_ERR_REM_USER_CONN_TERM:
      return "panel closed the link";
    case BLE_HS_ERR_HCI_BASE + BLE_ERR_CONN_TERM_LOCAL:
      return "closed by the ESP32";
    case BLE_HS_ERR_HCI_BASE + BLE_ERR_CONN_ESTABLISHMENT:
      return "connection failed to establish";
    default: {
      char b[16];
      snprintf(b, sizeof(b), "code 0x%X", reason);
      return b;
    }
  }
}

void PanelBLE::begin(const char* mac, const char* namePrefix) {
  mac_ = mac;
  mac_.toLowerCase();
  prefix_ = namePrefix;
  queue_ = xQueueCreate(8, sizeof(Notif));
  NimBLEDevice::init("lr-clock");
  NimBLEDevice::setMTU(517);
  client_ = NimBLEDevice::createClient();
  client_->setClientCallbacks(new Callbacks(this), true);
  client_->setConnectTimeout(8000);
  // 30-50 ms interval, no latency, long supervision timeout (units: 1.25 ms / 10 ms).
  client_->setConnectionParams(24, 40, 0, BLE_SUPERVISION_MS / 10);
}

bool PanelBLE::connected() const { return client_ && client_->isConnected() && wr_ && nt_ && linkUp_; }

void PanelBLE::onLinkLost(int reason) {
  if (!linkUp_) return;  // never fully connected: not a drop
  linkUp_ = false;
  stats_.drops++;
  stats_.lastReason = reason;
  stats_.lastCause = pendingCause_.length() ? pendingCause_ : bleReasonText(reason);
  stats_.lastDropMs = millis();
  lostAtMs_ = millis();
  pendingCause_ = "";
  Serial.printf("[ble] link lost: %s\n", stats_.lastCause.c_str());
}

void PanelBLE::disconnect(const char* cause) {
  pendingCause_ = String("ESP32: ") + cause;
  if (client_ && client_->isConnected()) client_->disconnect();
  wr_ = nt_ = nullptr;
}

void PanelBLE::flushNotifs() {
  Notif n;
  while (xQueueReceive((QueueHandle_t)queue_, &n, 0) == pdTRUE) {
  }
}

bool PanelBLE::waitFor(bool (*match)(const Notif&), uint32_t timeoutMs) {
  uint32_t start = millis();
  Notif n;
  while (millis() - start < timeoutMs) {
    if (xQueueReceive((QueueHandle_t)queue_, &n, pdMS_TO_TICKS(50)) == pdTRUE) {
      lastNotify_ = "";
      for (int i = 0; i < n.len; ++i) {
        char b[4];
        snprintf(b, sizeof(b), "%02X ", n.d[i]);
        lastNotify_ += b;
      }
      if (match(n)) return true;
    }
    if (!client_->isConnected()) return false;
  }
  return false;
}

bool PanelBLE::write(const uint8_t* d, size_t n, bool response) {
  if (!wr_) return false;
  size_t chunk = BLE_CHUNK;
  uint16_t mtu = client_->getMTU();
  if (mtu > 3 && chunk > (size_t)(mtu - 3)) chunk = mtu - 3;
  for (size_t off = 0; off < n; off += chunk) {
    size_t len = n - off < chunk ? n - off : chunk;
    if (!wr_->writeValue(d + off, len, response)) {
      err_ = "write failed";
      return false;
    }
  }
  return true;
}

bool PanelBLE::attach() {
  wr_ = nt_ = nullptr;
  for (NimBLERemoteService* svc : client_->getServices(true)) {
    if (!wr_) wr_ = svc->getCharacteristic(kWriteUuid);
    if (!nt_) nt_ = svc->getCharacteristic(kNotifyUuid);
  }
  if (!wr_ || !nt_) {
    err_ = "FA02/FA03 not found";
    return false;
  }
  QueueHandle_t q = (QueueHandle_t)queue_;
  bool ok = nt_->subscribe(true, [q](NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
    Notif n;
    n.len = len > sizeof(n.d) ? sizeof(n.d) : (uint8_t)len;
    memcpy(n.d, data, n.len);
    xQueueSend(q, &n, 0);
  });
  if (!ok) {
    err_ = "subscribe failed";
    return false;
  }
  return handshake();
}

bool PanelBLE::connect() {
  if (connected()) return true;
  err_ = "";
  uint32_t t0 = millis();
  bool linked = false;

  // 1) Fast path: straight to the address we found before, no scan.
  if (addr_) {
    linked = client_->connect(*(NimBLEAddress*)addr_, true, false, true);
    if (linked && !attach()) {
      disconnect("attach failed after direct connect");
      linked = false;
    }
  }

  // 2) Slow path: scan for the panel by MAC or name.
  if (!linked) {
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setActiveScan(true);
    NimBLEScanResults res = scan->getResults(4000, false);
    const NimBLEAdvertisedDevice* found = nullptr;
    for (int i = 0; i < res.getCount(); ++i) {
      const NimBLEAdvertisedDevice* d = res.getDevice(i);
      String addr = d->getAddress().toString().c_str();
      addr.toLowerCase();
      String name = d->getName().c_str();
      if (mac_.length() ? addr == mac_ : name.startsWith(prefix_)) {
        found = d;
        break;
      }
    }
    if (!found) {
      scan->clearResults();
      if (!err_.length()) err_ = "panel not found in scan";
      return false;
    }
    if (!addr_) addr_ = new NimBLEAddress(found->getAddress());
    else *(NimBLEAddress*)addr_ = found->getAddress();
    linked = client_->connect(found, true, false, true);
    scan->clearResults();
    if (!linked) {
      err_ = "connect failed";
      return false;
    }
    if (!attach()) {
      disconnect("attach failed");
      return false;
    }
  }

  linkUp_ = true;
  pendingCause_ = "";
  sinceValidation_ = 0;
  missedInARow_ = 0;
  stats_.connects++;
  if (lostAtMs_) {
    stats_.lastReconnectMs = millis() - lostAtMs_;
    lostAtMs_ = 0;
  }
  String note;
  if (stats_.drops) note = " after drop #" + String(stats_.drops) + ": " + stats_.lastCause;
  Serial.printf("[ble] connected in %lu ms (MTU %u)%s\n", (unsigned long)(millis() - t0), client_->getMTU(),
                note.c_str());
  return true;
}

bool PanelBLE::handshake() {
  struct tm t;
  time_t now = time(nullptr);
  localtime_r(&now, &t);
  bool timeOk = t.tm_year + 1900 >= 2024;
  uint8_t hello[8] = {0x08, 0x00, 0x01, 0x80,
                      (uint8_t)(timeOk ? t.tm_hour : 12), (uint8_t)(timeOk ? t.tm_min : 0),
                      (uint8_t)(timeOk ? t.tm_sec : 0), 0x00};
  flushNotifs();
  if (!write(hello, sizeof(hello), false)) return false;
  if (!waitFor([](const Notif& n) { return n.len >= 4 && n.d[2] == 0x01 && n.d[3] == 0x80; }, 5000)) {
    err_ = "no reply to hello (08 00 01 80)";
    return false;
  }
  delay(150);
  static const uint8_t version[4] = {0x04, 0x00, 0x05, 0x80};
  if (!write(version, sizeof(version), false)) return false;
  // Some units never answer this one; the repo treats that as fine.
  waitFor([](const Notif& n) { return n.len >= 4 && n.d[2] == 0x05 && n.d[3] == 0x80; }, 2000);
  delay(150);
  return true;
}

bool PanelBLE::sendFrame(const uint8_t* frame, size_t len) {
  if (!connected() && !connect()) return false;
  flushNotifs();
  // Write-with-response: the repo found it much more stable on ACT1025.
  if (!write(frame, len, true)) {
    disconnect("frame write failed");
    return false;
  }
  bool acked = waitFor([](const Notif& n) { return n.len == 5 && n.d[0] == 0x05 && n.d[2] == 0x02; }, 5000);
  if (!acked) {
    stats_.missedAcks++;
    err_ = "no frame ACK";
    // A single lost ACK used to drop the link (panel falls back to its own
    // screen). Now the frame is simply re-sent; only a run of misses or a
    // dead link triggers a reconnect.
    if (!client_->isConnected() || ++missedInARow_ >= kMaxMissedAcks) {
      disconnect("no frame ACK");
    }
    return false;
  }
  missedInARow_ = 0;
  ++frames_;
#if BLE_SEND_VALIDATION
  if (++sinceValidation_ >= 30) {
    static const uint8_t validation[5] = {0x05, 0x00, 0x00, 0x01, 0x00};
    write(validation, sizeof(validation), false);
    sinceValidation_ = 0;
  }
#endif
  return true;
}

bool PanelBLE::setBrightness(uint8_t level) {
  if (!connected()) return false;
  if (level > 100) level = 100;
  uint8_t cmd[5] = {0x05, 0x00, 0x04, 0x80, level};
  return write(cmd, sizeof(cmd), true);
}
