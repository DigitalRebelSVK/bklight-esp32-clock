#include "ota.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>

#include "config_defaults.h"

// Arduino normally approves a freshly updated image the moment it boots. Ask it
// not to: loop() approves it once Wi-Fi works, so a crashing build rolls back.
extern "C" bool verifyRollbackLater() { return true; }

namespace ota {
namespace {

constexpr uint32_t kHealthyAfterMs = 30000;  // on Wi-Fi this long -> keep the new image
constexpr uint32_t kGiveUpMs = 300000;       // still offline after this -> roll back
const char* const kUser = "admin";

WebServer* web = nullptr;
ShowFn showFn = nullptr;
String fwId = "?";
bool pendingVerify = false;
bool verified = false;
bool networkStarted = false;

// Browser upload state (one upload at a time).
bool uploadAuthed = false;
String uploadError;
size_t uploadBytes = 0;

void show(const char* text, int seconds) {
  if (showFn) showFn(text, seconds);
}

bool passwordSet() { return OTA_PASSWORD[0] != '\0'; }

bool authOk() { return !passwordSet() || web->authenticate(kUser, OTA_PASSWORD); }

String kb(size_t bytes) { return String((bytes + 512) / 1024) + " KB"; }

const char kPage[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Clock firmware</title>
<style>
:root{color-scheme:dark}
body{font:15px/1.4 system-ui,sans-serif;background:#111;color:#ddd;margin:0 auto;padding:16px;max-width:520px}
h1{font-size:19px;margin:0 0 6px}
.n{color:#9a9a9a;font-size:13px}
a{color:#ffb35c}
input{margin:14px 0;color:#ddd}
button{padding:9px 18px;border-radius:9px;border:0;background:#ffb35c;color:#111;font:inherit;font-weight:600;cursor:pointer}
button:disabled{opacity:.4;cursor:default}
progress{width:100%;height:14px;accent-color:#ffb35c;margin-top:14px}
#m{margin-top:10px;min-height:1.4em}
</style></head><body>
<h1>Firmware update</h1>
<div class="n" id="fw">reading…</div>
<p class="n">Arduino IDE: <b>Sketch → Export Compiled Binary</b>, then pick
the <b>.ino.bin</b> file from the sketch's <b>build</b> folder. Not the merged,
bootloader or partitions file.</p>
<input type="file" id="f" accept=".bin"><br>
<button id="go">Upload</button>
<progress id="p" max="100" value="0" hidden></progress>
<div id="m"></div>
<p><a href="/">← back to settings</a></p>
<script>
const $=id=>document.getElementById(id);
let before='';
function info(){return fetch('/api/state',{cache:'no-store'}).then(r=>r.json()).then(j=>{
  $('fw').textContent='running '+j.status.fwInfo;return j.status.fw;});}
info().then(id=>before=id).catch(()=>{$('fw').textContent='clock not reachable';});
function say(t){$('m').textContent=t;}
$('go').onclick=()=>{
  const f=$('f').files[0];
  if(!f)return say('Pick a .bin file first.');
  if(/merged|bootloader|partitions/i.test(f.name))return say('That is not the app image. Pick the .ino.bin file.');
  const x=new XMLHttpRequest(),d=new FormData();
  d.append('fw',f,f.name);
  x.open('POST','/update?size='+f.size);
  $('p').hidden=false;$('p').value=0;$('go').disabled=true;say('Uploading…');
  x.upload.onprogress=e=>{if(e.lengthComputable)$('p').value=e.loaded*100/e.total;};
  x.onload=()=>{
    if(x.status==200){say('Written. Restarting…');setTimeout(wait,4000);}
    else{say(x.responseText||('Error '+x.status));$('go').disabled=false;}
  };
  x.onerror=()=>{say('Connection lost during upload.');$('go').disabled=false;};
  x.send(d);
};
function wait(){
  let n=0;
  const t=setInterval(()=>{info().then(id=>{
    clearInterval(t);$('go').disabled=false;
    say(id!==before?'Back online, now running '+id+' (was '+before+').'
                   :'Back online, but still on '+id+'. The new firmware was rolled back or identical.');
    before=id;
  }).catch(()=>{if(++n>40){clearInterval(t);say('Not back after 2 minutes. Look at the panel or /status.txt.');}});},3000);
}
</script>
</body></html>)HTML";

void handlePage() {
  if (!authOk()) return web->requestAuthentication(BASIC_AUTH, "clock");
  web->send(200, "text/html; charset=utf-8", kPage);
}

// Called for every chunk of the multipart body.
void handleChunk() {
  HTTPUpload& up = web->upload();
  switch (up.status) {
    case UPLOAD_FILE_START: {
      uploadAuthed = authOk();
      uploadError = uploadAuthed ? "" : "not authorized";
      uploadBytes = 0;
      if (!uploadAuthed) break;
      if (!available()) {
        uploadError = "no OTA slot - switch the partition scheme to 'Minimal SPIFFS (... with OTA)' and flash once over USB";
        break;
      }
      size_t size = (size_t)web->arg("size").toInt();
      Serial.printf("[ota] web upload %s (%u B)\n", up.filename.c_str(), (unsigned)size);
      show("FIRMWARE UPDATE", 50);
      if (!Update.begin(size ? size : UPDATE_SIZE_UNKNOWN, U_FLASH)) uploadError = Update.errorString();
      break;
    }
    case UPLOAD_FILE_WRITE:
      if (uploadError.length()) break;
      if (Update.write(up.buf, up.currentSize) != up.currentSize) {
        uploadError = Update.errorString();
        Update.abort();
      }
      uploadBytes += up.currentSize;
      break;
    case UPLOAD_FILE_END:
      if (uploadError.isEmpty() && !Update.end(true)) uploadError = Update.errorString();
      break;
    case UPLOAD_FILE_ABORTED:
      if (Update.isRunning()) Update.abort();
      uploadError = "upload aborted";
      break;
    default:
      break;
  }
}

// Called once the whole body has been read.
void handleDone() {
  if (!uploadAuthed) return web->requestAuthentication(BASIC_AUTH, "clock");
  web->sendHeader("Connection", "close");
  if (uploadError.length()) {
    Serial.printf("[ota] web upload failed: %s\n", uploadError.c_str());
    show("UPDATE FAILED", 15);
    web->send(500, "text/plain", "update failed: " + uploadError + "\n");
    return;
  }
  Serial.printf("[ota] web upload ok, %u B, restarting\n", (unsigned)uploadBytes);
  web->send(200, "text/plain", "ok " + String(uploadBytes) + " bytes, restarting\n");
  show("RESTART", 10);
  delay(300);  // let the response leave
  ESP.restart();
}

}  // namespace

bool available() { return esp_ota_get_next_update_partition(nullptr) != nullptr; }

void begin(WebServer& server, ShowFn showText) {
  web = &server;
  showFn = showText;

  const esp_partition_t* running = esp_ota_get_running_partition();
  uint8_t sha[32];
  if (running && esp_partition_get_sha256(running, sha) == ESP_OK) {
    char b[9];
    snprintf(b, sizeof(b), "%02x%02x%02x%02x", sha[0], sha[1], sha[2], sha[3]);
    fwId = b;
  }
  esp_ota_img_states_t st;
  pendingVerify = running && esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  Serial.printf("[ota] firmware %s on %s%s%s\n", fwId.c_str(), running ? running->label : "?",
                pendingVerify ? ", new - waiting for Wi-Fi to confirm" : "",
                available() ? "" : ", NO OTA SLOT (partition scheme)");

  server.on("/update", HTTP_GET, handlePage);
  server.on("/update", HTTP_POST, handleDone, handleChunk);
}

void startNetwork() {
  if (networkStarted || !available()) return;
  networkStarted = true;
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setMdnsEnabled(false);  // mDNS already runs in main as HOSTNAME.local
  if (passwordSet()) ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([] {
    Serial.println("[ota] IDE upload started");
    show("FIRMWARE UPDATE", 50);
  });
  ArduinoOTA.onEnd([] {
    Serial.println("[ota] IDE upload done, restarting");
    show("RESTART", 10);
  });
  ArduinoOTA.onError([](ota_error_t e) {
    Serial.printf("[ota] IDE upload failed (%u)\n", (unsigned)e);
    show("UPDATE FAILED", 15);
  });
  ArduinoOTA.begin();
  MDNS.enableArduino(3232, passwordSet());  // makes the IDE list "clock at <ip>"
  Serial.println("[ota] network port ready");
}

void loop(bool wifiUp) {
  if (networkStarted) ArduinoOTA.handle();

  if (!pendingVerify || verified) return;
  uint32_t up = millis();
  if (wifiUp && up > kHealthyAfterMs) {
    esp_ota_mark_app_valid_cancel_rollback();
    verified = true;
    Serial.println("[ota] new firmware confirmed");
  } else if (up > kGiveUpMs) {
    Serial.println("[ota] no Wi-Fi on the new firmware, rolling back");
    show("ROLLBACK", 10);
    delay(200);
    esp_ota_mark_app_invalid_rollback_and_reboot();
  }
}

String firmwareId() { return fwId; }

String status() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  String s = fwId + ", " + String(running ? running->label : "?") + ", " + kb(ESP.getSketchSize()) + " of " +
             kb(running ? running->size : 0);
  if (!available()) s += ", NO OTA SLOT - change partition scheme";
  else if (pendingVerify && !verified) s += ", new - confirming";
  else if (verified) s += ", new - confirmed";
#ifndef CONFIG_APP_ROLLBACK_ENABLE
  s += ", no rollback in this core";
#endif
  return s;
}

}  // namespace ota
