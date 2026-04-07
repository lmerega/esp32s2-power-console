#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

WebServer server(80);
Preferences prefs;
WiFiManager wm;

#define FW_VERSION "RECOVERY-1.0.0"

const char *WIFI_PREFS_NAMESPACE = "pcpower";
const char *WIFI_SSID_KEY = "wifi_ssid";
const char *WIFI_PASS_KEY = "wifi_pass";
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 30000;
const unsigned long WIFI_CONNECT_QUICK_TIMEOUT_MS = 12000;
const unsigned long WIFI_RETRY_MS = 5000;
const char *RECOVERY_AP_SSID_PREFIX = "ESP32S2-Recovery-";
const char *OTA_UPLOAD_HEADERS[] = {"X-File-Size"};
const size_t OTA_UPLOAD_HEADERS_COUNT = 1;

String wifiSsidConfig = "";
String wifiPassConfig = "";
String recoveryApSsid = "";
String recoveryApPass = "";
String wifiState = "init";
String wifiLastError = "";
bool recoveryApActive = false;
bool recoveryPortalActive = false;
unsigned long wifiLastRetryAtMs = 0;

bool otaInProgress = false;
bool otaRebootScheduled = false;
unsigned long otaRebootAtMs = 0;
String otaStatus = "idle";
uint32_t otaExpectedBytes = 0;
uint32_t otaWrittenBytes = 0;
String otaTargetLabel = "";

uint32_t bootSessionId = 0;

String jsonEscape(const String &src) {
  String out;
  out.reserve(src.length() + 8);
  for (size_t i = 0; i < src.length(); i++) {
    char c = src.charAt(i);
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if ((uint8_t)c < 0x20) out += ' ';
        else out += c;
        break;
    }
  }
  return out;
}

int rssiToPercent(int rssi) {
  if (rssi <= -100) return 0;
  if (rssi >= -50) return 100;
  return 2 * (rssi + 100);
}

void sendJson(int code, const String &body) {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "application/json", body);
}

void sendText(int code, const String &body) {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "text/plain", body);
}

void loadWifiConfig() {
  prefs.begin(WIFI_PREFS_NAMESPACE, true);
  wifiSsidConfig = prefs.getString(WIFI_SSID_KEY, "");
  wifiPassConfig = prefs.getString(WIFI_PASS_KEY, "");
  prefs.end();
}

void saveWifiConfig() {
  prefs.begin(WIFI_PREFS_NAMESPACE, false);
  prefs.putString(WIFI_SSID_KEY, wifiSsidConfig);
  prefs.putString(WIFI_PASS_KEY, wifiPassConfig);
  prefs.end();
}

String chipIdSuffixHex() {
  uint64_t chipId = ESP.getEfuseMac();
  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06X", (uint32_t)(chipId & 0xFFFFFFULL));
  return String(suffix);
}

void initRecoveryApCredentials() {
  String suffix = chipIdSuffixHex();
  recoveryApSsid = String(RECOVERY_AP_SSID_PREFIX) + suffix;
  recoveryApPass = "";
}

void startRecoveryAp(const String &reason) {
  if (recoveryPortalActive) {
    recoveryApActive = true;
    wifiState = "ap_fallback";
    if (reason.length() > 0) wifiLastError = reason;
    return;
  }

  if (recoveryApSsid.length() == 0) {
    initRecoveryApCredentials();
  }

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPdisconnect(true);
  delay(40);
  if (!WiFi.softAP(recoveryApSsid.c_str())) {
    wifiLastError = "ap_start_failed";
    recoveryApActive = false;
    recoveryPortalActive = false;
    return;
  }

  wm.setConfigPortalBlocking(false);
  wm.setCaptivePortalEnable(true);
  wm.setAPClientCheck(true);
  wm.setConfigPortalTimeout(0);
  wm.startConfigPortal(recoveryApSsid.c_str());

  delay(40);
  if (WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
    wifiLastError = "ap_start_failed";
    recoveryApActive = false;
    recoveryPortalActive = false;
    return;
  }

  recoveryApActive = true;
  recoveryPortalActive = true;
  wifiState = "ap_fallback";
  if (reason.length() > 0) wifiLastError = reason;
}

void stopRecoveryAp() {
  if (recoveryPortalActive) {
    wm.stopConfigPortal();
    recoveryPortalActive = false;
  }
  if (!recoveryApActive) return;
  WiFi.softAPdisconnect(true);
  recoveryApActive = false;
}

bool connectConfiguredWifi(unsigned long timeoutMs) {
  if (wifiSsidConfig.length() == 0) {
    wifiState = "no_credentials";
    wifiLastError = "missing_wifi_credentials";
    return false;
  }

  wifiState = "sta_connecting";
  wifiLastError = "";
  WiFi.begin(wifiSsidConfig.c_str(), wifiPassConfig.c_str());
  unsigned long started = millis();
  while (millis() - started < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) {
      wifiState = "sta_connected";
      wifiLastError = "";
      stopRecoveryAp();
      return true;
    }
    delay(250);
  }

  wifiState = "sta_failed";
  wifiLastError = "sta_connect_timeout";
  return false;
}

String bootSessionIdHex() {
  String out = String(bootSessionId, HEX);
  out.toUpperCase();
  return out;
}

String runningPartitionLabel() {
  const esp_partition_t *part = esp_ota_get_running_partition();
  if (part == nullptr) return "unknown";
  return String(part->label);
}

const esp_partition_t *resolveMainOtaTarget() {
  const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
  if (target == nullptr) return nullptr;
  if (target->type != ESP_PARTITION_TYPE_APP) return nullptr;

  const bool isMainSlot =
    (target->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) ||
    (target->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1);
  if (!isMainSlot) return nullptr;

  return target;
}

String statusJson() {
  String json = "{";
  json += "\"ok\":true,";
  json += "\"fw_version\":\"" + jsonEscape(String(FW_VERSION)) + "\",";
  json += "\"boot_id\":\"" + jsonEscape(bootSessionIdHex()) + "\",";
  json += "\"uptime_s\":" + String(millis() / 1000UL) + ",";
  json += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"wifi_ssid\":\"" + jsonEscape(WiFi.SSID()) + "\",";
  json += "\"wifi_saved_ssid\":\"" + jsonEscape(wifiSsidConfig) + "\",";
  json += "\"ip\":\"" + jsonEscape(WiFi.localIP().toString()) + "\",";
  json += "\"wifi_state\":\"" + jsonEscape(wifiState) + "\",";
  json += "\"wifi_last_error\":\"" + jsonEscape(wifiLastError) + "\",";
  json += "\"recovery_ap_active\":" + String(recoveryApActive ? "true" : "false") + ",";
  json += "\"recovery_ap_ssid\":\"" + jsonEscape(recoveryApSsid) + "\",";
  json += "\"recovery_ap_ip\":\"" + jsonEscape(WiFi.softAPIP().toString()) + "\",";
  json += "\"ota_in_progress\":" + String(otaInProgress ? "true" : "false") + ",";
  json += "\"ota_status\":\"" + jsonEscape(otaStatus) + "\",";
  json += "\"running_partition\":\"" + jsonEscape(runningPartitionLabel()) + "\",";
  json += "\"ota_target_partition\":\"" + jsonEscape(otaTargetLabel) + "\"";
  json += "}";
  return json;
}

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="it">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP32 Recovery OTA</title>
  <style>
    body{font-family:Segoe UI,Tahoma,sans-serif;margin:0;padding:16px;background:#0f172a;color:#e5e7eb}
    .card{max-width:760px;margin:0 auto;border:1px solid #334155;border-radius:14px;padding:16px;background:#111827}
    h1{margin:0 0 10px 0;font-size:1.2rem}
    .muted{color:#94a3b8}
    .row{display:flex;gap:8px;flex-wrap:wrap}
    .row input{flex:1;min-width:220px;padding:10px;border-radius:8px;border:1px solid #475569;background:#0b1220;color:#e5e7eb}
    input[type=file]{width:100%;padding:10px;border-radius:8px;border:1px solid #475569;background:#0b1220;color:#e5e7eb}
    button{margin-top:12px;padding:10px 14px;border:0;border-radius:10px;background:#0f766e;color:#fff;font-weight:700;cursor:pointer}
    button.secondary{background:#1d4ed8}
    progress{width:100%;margin-top:12px}
    pre{white-space:pre-wrap;background:#0b1220;padding:10px;border-radius:8px;border:1px solid #334155}
  </style>
</head>
<body>
  <div class="card">
    <h1>ESP32 Recovery OTA</h1>
    <div class="muted">Firmware recovery: se STA fallisce, AP di emergenza automatico. Carica un <code>.bin</code> del firmware principale.</div>
    <div style="margin-top:12px">
      <div class="row">
        <select id="scanList" style="flex:1;min-width:220px;padding:10px;border-radius:8px;border:1px solid #475569;background:#0b1220;color:#e5e7eb">
          <option value="">Rete trovata</option>
        </select>
        <button class="secondary" onclick="scanWifi()">Scansiona reti</button>
      </div>
      <div class="row" style="margin-top:8px">
        <input id="wifiSsid" type="text" maxlength="32" placeholder="Nuovo SSID WiFi">
        <input id="wifiPass" type="password" maxlength="64" placeholder="Nuova password WiFi">
      </div>
      <button class="secondary" onclick="saveWifi()">Salva WiFi Recovery</button>
    </div>
    <div style="margin-top:12px">
      <input id="binFile" type="file" accept=".bin,application/octet-stream">
      <button onclick="uploadFirmware()">Carica firmware</button>
      <progress id="prog" value="0" max="100" style="display:none"></progress>
      <pre id="status">Pronto</pre>
    </div>
  </div>
  <script>
    function setStatus(msg){ document.getElementById('status').textContent = msg; }
    async function refreshStatus(){
      try{
        const r = await fetch('/api/status', {cache:'no-store'});
        const j = await r.json();
        document.getElementById('wifiSsid').value = j.wifi_saved_ssid || '';
        const apInfo = j.recovery_ap_active ? `AP ${j.recovery_ap_ssid} @ ${j.recovery_ap_ip}` : 'AP off';
        setStatus(`WiFi state=${j.wifi_state} connected=${j.wifi_connected} ip=${j.ip}\n${apInfo}\nOTA=${j.ota_status} target=${j.ota_target_partition}`);
      } catch(e) {}
    }
    async function saveWifi(){
      const ssid = document.getElementById('wifiSsid').value.trim();
      const pass = document.getElementById('wifiPass').value;
      if(!ssid){ setStatus('SSID obbligatorio'); return; }
      const body = new URLSearchParams();
      body.set('ssid', ssid);
      body.set('password', pass);
      try{
        const r = await fetch('/api/config/wifi', {method:'POST', body});
        const t = await r.text();
        setStatus(`WiFi config HTTP ${r.status}: ${t}`);
        refreshStatus();
      } catch(e){
        setStatus('Salvataggio WiFi fallito');
      }
    }
    async function scanWifi(){
      const sel = document.getElementById('scanList');
      sel.innerHTML = '<option value=\"\">Scansione in corso...</option>';
      try{
        const r = await fetch('/api/wifi/scan', {cache:'no-store'});
        const j = await r.json();
        if(!j || !j.ok){
          sel.innerHTML = '<option value=\"\">Scan fallita</option>';
          setStatus('Scan WiFi fallita');
          return;
        }
        const list = Array.isArray(j.networks) ? j.networks : [];
        sel.innerHTML = '<option value=\"\">Rete trovata</option>';
        for(const n of list){
          const ssid = (n.ssid || '').trim();
          if(!ssid) continue;
          const pct = Number.isFinite(n.signal_pct) ? n.signal_pct : 0;
          const opt = document.createElement('option');
          opt.value = ssid;
          opt.textContent = `${ssid} (${pct}%)`;
          sel.appendChild(opt);
        }
        setStatus(`Scan completata: ${list.length} reti`);
      } catch(e){
        sel.innerHTML = '<option value=\"\">Scan fallita</option>';
        setStatus('Scan WiFi fallita');
      }
    }
    document.getElementById('scanList').addEventListener('change', (ev) => {
      const v = (ev.target.value || '').trim();
      if(v) document.getElementById('wifiSsid').value = v;
    });
    function uploadFirmware(){
      const fileInput = document.getElementById('binFile');
      if(!fileInput.files.length){ setStatus('Seleziona un file .bin'); return; }
      const file = fileInput.files[0];
      const formData = new FormData();
      formData.append('update', file, file.name);
      const xhr = new XMLHttpRequest();
      const prog = document.getElementById('prog');
      prog.value = 0;
      prog.style.display = 'block';
      xhr.open('POST', '/api/update');
      xhr.setRequestHeader('X-File-Size', String(file.size));
      xhr.upload.onprogress = (e) => {
        if(!e.lengthComputable) return;
        const pct = Math.round((e.loaded / e.total) * 100);
        prog.value = pct;
        setStatus(`Upload rete ${pct}%`);
      };
      xhr.onload = () => {
        if(xhr.status === 200){
          prog.value = 100;
          setStatus('Aggiornamento completato. Riavvio in corso...');
        } else {
          setStatus(`HTTP ${xhr.status}: ${xhr.responseText || 'errore'}`);
        }
      };
      xhr.onerror = () => setStatus('Upload fallito');
      xhr.send(formData);
    }
    refreshStatus();
    scanWifi();
    setInterval(refreshStatus, 3000);
  </script>
</body>
</html>
)HTML";

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleApiStatus() {
  sendJson(200, statusJson());
}

void handleApiSetWifi() {
  if (!server.hasArg("ssid")) {
    sendJson(400, "{\"ok\":false,\"error\":\"missing_ssid\"}");
    return;
  }

  String nextSsid = server.arg("ssid");
  String nextPass = server.hasArg("password") ? server.arg("password") : "";
  nextSsid.trim();

  if (nextSsid.length() == 0 || nextSsid.length() > 32 || nextPass.length() > 64) {
    sendJson(400, "{\"ok\":false,\"error\":\"invalid_wifi_values\"}");
    return;
  }

  wifiSsidConfig = nextSsid;
  wifiPassConfig = nextPass;
  saveWifiConfig();

  WiFi.disconnect(true, true);
  bool connected = connectConfiguredWifi(WIFI_CONNECT_QUICK_TIMEOUT_MS);
  if (!connected) {
    startRecoveryAp("sta_connect_failed_after_update");
    sendJson(200, "{\"ok\":true,\"message\":\"wifi_saved_ap_fallback_active\"}");
    return;
  }

  sendJson(200, "{\"ok\":true,\"message\":\"wifi_saved_sta_connected\"}");
}

void handleApiSetWifiMethodNotAllowed() {
  sendJson(405, "{\"ok\":false,\"error\":\"use_post\"}");
}

void handleApiWifiScan() {
  int found = WiFi.scanNetworks(false, true);
  if (found < 0) {
    sendJson(500, "{\"ok\":false,\"error\":\"scan_failed\"}");
    return;
  }

  String json = "{\"ok\":true,\"count\":" + String(found) + ",\"networks\":[";
  for (int i = 0; i < found; i++) {
    if (i > 0) json += ",";
    String ssid = WiFi.SSID(i);
    int rssi = WiFi.RSSI(i);
    bool isOpen = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    json += "{";
    json += "\"ssid\":\"" + jsonEscape(ssid) + "\",";
    json += "\"rssi\":" + String(rssi) + ",";
    json += "\"signal_pct\":" + String(rssiToPercent(rssi)) + ",";
    json += "\"open\":" + String(isOpen ? "true" : "false");
    json += "}";
  }
  json += "]}";
  WiFi.scanDelete();
  sendJson(200, json);
}

void handleApiReboot() {
  otaInProgress = true;
  otaStatus = "reboot_scheduled";
  otaRebootScheduled = true;
  otaRebootAtMs = millis() + 1500;
  sendJson(200, "{\"ok\":true,\"message\":\"reboot_scheduled\"}");
}

void handleApiUpdateUpload() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaInProgress = true;
    otaStatus = "uploading";
    otaExpectedBytes = 0;
    otaWrittenBytes = 0;
    otaTargetLabel = "";
    if (server.hasHeader("X-File-Size")) {
      otaExpectedBytes = (uint32_t)strtoul(server.header("X-File-Size").c_str(), nullptr, 10);
    }
    const esp_partition_t *target = resolveMainOtaTarget();
    if (target == nullptr) {
      otaStatus = "invalid_target_partition";
      Update.abort();
      yield();
      return;
    }
    otaTargetLabel = String(target->label);
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH, -1, LOW, target->label)) {
      otaStatus = "begin_failed";
      Update.printError(Serial);
    }
    yield();
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (otaStatus == "begin_failed") {
      yield();
      return;
    }
    size_t written = Update.write(upload.buf, upload.currentSize);
    otaWrittenBytes += (uint32_t)written;
    if (written != upload.currentSize) {
      Update.abort();
      otaStatus = "write_failed";
      Update.printError(Serial);
    }
    yield();
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (otaExpectedBytes > 0 && otaWrittenBytes < otaExpectedBytes) {
      Update.abort();
      otaStatus = "incomplete_upload";
      return;
    }
    if (Update.end(true)) {
      otaStatus = "flash_ok";
    } else {
      otaStatus = "end_failed";
      Update.printError(Serial);
    }
    yield();
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaStatus = "aborted";
  }
}

void handleApiUpdate() {
  bool ok = (otaStatus == "flash_ok") && !Update.hasError();
  if (ok) {
    otaStatus = "reboot_scheduled";
    otaRebootScheduled = true;
    otaRebootAtMs = millis() + 2500;
    sendJson(200, "{\"ok\":true,\"message\":\"update_complete_reboot_scheduled\"}");
  } else {
    otaInProgress = false;
    otaStatus = "update_failed";
    sendJson(500, "{\"ok\":false,\"error\":\"update_failed\"}");
  }
}

void setupRoutes() {
  server.collectHeaders(OTA_UPLOAD_HEADERS, OTA_UPLOAD_HEADERS_COUNT);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/wifi/scan", HTTP_GET, handleApiWifiScan);
  server.on("/api/config/wifi", HTTP_POST, handleApiSetWifi);
  server.on("/api/config/wifi", HTTP_GET, handleApiSetWifiMethodNotAllowed);
  server.on("/api/reboot", HTTP_GET, handleApiReboot);
  server.on("/api/update", HTTP_POST, handleApiUpdate, handleApiUpdateUpload);
  server.onNotFound([]() { sendText(404, "Not found"); });
}

void setup() {
  Serial.begin(115200);
  delay(300);

  bootSessionId = esp_random();
  if (bootSessionId == 0) bootSessionId = 1;

  initRecoveryApCredentials();
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);

  loadWifiConfig();
  bool connected = connectConfiguredWifi(WIFI_CONNECT_TIMEOUT_MS);
  if (!connected) {
    startRecoveryAp("boot_sta_connect_failed");
  }

  setupRoutes();
  server.begin();
}

void loop() {
  if (recoveryPortalActive) {
    if (WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
      recoveryPortalActive = false;
      recoveryApActive = false;
      startRecoveryAp("portal_ap_lost_restart");
    }
    wm.process();
  }

  if (otaRebootScheduled && (long)(millis() - otaRebootAtMs) >= 0) {
    delay(100);
    ESP.restart();
    return;
  }

  server.handleClient();

  if (WiFi.status() == WL_CONNECTED) {
    wifiState = "sta_connected";
    wifiLastError = "";
    stopRecoveryAp();
  } else if (millis() > wifiLastRetryAtMs) {
    wifiLastRetryAtMs = millis() + WIFI_RETRY_MS;
    if (wifiSsidConfig.length() > 0) {
      WiFi.disconnect();
      WiFi.begin(wifiSsidConfig.c_str(), wifiPassConfig.c_str());
      wifiState = "sta_reconnecting";
    } else {
      wifiState = "no_credentials";
      wifiLastError = "missing_wifi_credentials";
    }

    if (!recoveryApActive) {
      startRecoveryAp("runtime_sta_not_connected");
    }
  }
}
