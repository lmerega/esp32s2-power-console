#include "wifi_diagnostics.h"

#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "buglog.h"

namespace {
constexpr uint32_t kLogIntervalMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kSignalSampleMs = 1000;
constexpr uint8_t kEventQueueSize = 16;

struct WifiEvent {
  arduino_event_id_t type;
  uint32_t atMs;
  uint8_t reason;
  int8_t rssi;
  uint8_t channel;
  uint8_t bssid[6];
  uint32_t ip;
};

QueueHandle_t eventQueue = nullptr;
portMUX_TYPE dropsMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t eventDrops = 0;
uint32_t logDrops = 0;
uint32_t reportedDrops = 0;
bool eventsEnabled = false;
bool offline = true;
uint32_t offlineSinceMs = 0;
uint32_t lastOutageMs = 0;
uint32_t disconnects = 0;
uint32_t gotIpEvents = 0;
uint32_t lostIpEvents = 0;
uint32_t reconnectCalls = 0;
uint32_t reconnectFailures = 0;
uint32_t outageReconnectCalls = 0;
uint32_t lastDisconnectMs = 0;
uint8_t lastReason = 0;
uint8_t outageFirstReason = 0;
int32_t lastDisconnectRssi = 0;
int32_t lastRssi = 0;
uint8_t lastChannel = 0;
uint8_t lastBssid[6] = {};
uint32_t lastSampleMs = 0;
bool disconnectLogged = false;
uint32_t lastDisconnectLogMs = 0;
bool retryLogged = false;
uint32_t lastRetryLogMs = 0;
uint32_t lastDropLogMs = 0;
bool scanActive = false;
uint32_t scanStartedMs = 0;
uint32_t scanCount = 0;
uint32_t scanDurationMs = 0;
int lastScanResult = 0;
const char *lastScanOutcome = "never";
uint32_t scanRejectedCount = 0;
uint32_t lastScanRejectedLogMs = 0;

void recordDiagnostic(BugSeverity severity, uint16_t code, const char *message) {
  if (log_event(severity, code, message)) return;
  // The existing buglog queue is shared with boot/heap/watchdog records.
  buglog_flush(8);
  if (!log_event(severity, code, message)) ++logDrops;
}

uint32_t droppedEvents() {
  portENTER_CRITICAL(&dropsMux);
  uint32_t value = eventDrops;
  portEXIT_CRITICAL(&dropsMux);
  return value;
}

// Runs on the Arduino network event task: no Strings, flash, locks or WiFi calls.
void onWifiEvent(arduino_event_id_t type, arduino_event_info_t info) {
  WifiEvent event{};
  event.type = type;
  event.atMs = millis();
  switch (type) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      event.reason = info.wifi_sta_disconnected.reason;
      event.rssi = info.wifi_sta_disconnected.rssi;
      memcpy(event.bssid, info.wifi_sta_disconnected.bssid, sizeof(event.bssid));
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      event.channel = info.wifi_sta_connected.channel;
      memcpy(event.bssid, info.wifi_sta_connected.bssid, sizeof(event.bssid));
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      event.ip = info.got_ip.ip_info.ip.addr;
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
    case ARDUINO_EVENT_WIFI_STA_START:
    case ARDUINO_EVENT_WIFI_STA_STOP:
      break;
    default:
      return;
  }
  if (xQueueSend(eventQueue, &event, 0) != pdTRUE) {
    portENTER_CRITICAL(&dropsMux);
    ++eventDrops;
    portEXIT_CRITICAL(&dropsMux);
  }
}

void sampleSignal() {
  if (WiFi.status() != WL_CONNECTED) return;
  lastRssi = WiFi.RSSI();
  lastChannel = WiFi.channel();
  const uint8_t *bssid = WiFi.BSSID();
  if (bssid != nullptr) memcpy(lastBssid, bssid, sizeof(lastBssid));
}

const char *reasonName() {
  return lastReason == 0 ? "NONE" : WiFi.disconnectReasonName(static_cast<wifi_err_reason_t>(lastReason));
}

void logDisconnect(uint32_t now) {
  char message[64];
  snprintf(message, sizeof(message), "disc r=%u %.24s sig=%ld ch=%u scan=%u",
           lastReason, reasonName(), static_cast<long>(lastRssi), lastChannel, scanActive ? 1U : 0U);
  recordDiagnostic(BUGLOG_WARN, 1700, message);
  lastDisconnectLogMs = now;
  disconnectLogged = true;
}

void processEvent(const WifiEvent &event) {
  char message[64];
  switch (event.type) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      ++disconnects;
      lastReason = event.reason;
      lastDisconnectMs = event.atMs;
      if (event.rssi < 0) lastRssi = event.rssi;
      lastDisconnectRssi = lastRssi;
      // Failed association attempts can contain an all-zero BSSID.
      if (event.bssid[0] | event.bssid[1] | event.bssid[2] | event.bssid[3] | event.bssid[4] | event.bssid[5]) {
        memcpy(lastBssid, event.bssid, sizeof(lastBssid));
      }
      if (!offline) {
        offline = true;
        offlineSinceMs = event.atMs;
        outageReconnectCalls = 0;
        retryLogged = false;
        disconnectLogged = false;
        outageFirstReason = event.reason;
      }
      if (outageFirstReason == 0) outageFirstReason = event.reason;
      // Keep all counters/reasons in RAM; coalesce repeated failures in flash.
      if (!disconnectLogged || static_cast<uint32_t>(event.atMs - lastDisconnectLogMs) >= kLogIntervalMs) {
        logDisconnect(event.atMs);
      }
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      lastChannel = event.channel;
      memcpy(lastBssid, event.bssid, sizeof(lastBssid));
      // Association is not yet an IP connection. The GOT_IP record is the outcome.
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
      ++gotIpEvents;
      lastOutageMs = offline ? static_cast<uint32_t>(event.atMs - offlineSinceMs) : 0;
      offline = false;
      sampleSignal();
      IPAddress ip(event.ip);
      snprintf(message, sizeof(message), "ip=%s off=%lus tries=%lu sig=%ld ch=%u",
               ip.toString().c_str(), static_cast<unsigned long>(lastOutageMs / 1000),
               static_cast<unsigned long>(outageReconnectCalls), static_cast<long>(lastRssi), lastChannel);
      recordDiagnostic(BUGLOG_INFO, 1701, message);
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      ++lostIpEvents;
      if (!offline) {
        offline = true;
        offlineSinceMs = event.atMs;
        outageReconnectCalls = 0;
        retryLogged = false;
        outageFirstReason = 0;
        disconnectLogged = false;
      }
      snprintf(message, sizeof(message), "lost_ip n=%lu st=%d scan=%u",
               static_cast<unsigned long>(lostIpEvents), static_cast<int>(WiFi.status()), scanActive ? 1U : 0U);
      recordDiagnostic(BUGLOG_WARN, 1702, message);
      break;
    case ARDUINO_EVENT_WIFI_STA_START:
    case ARDUINO_EVENT_WIFI_STA_STOP:
      recordDiagnostic(BUGLOG_INFO, 1704, event.type == ARDUINO_EVENT_WIFI_STA_START ? "sta_start" : "sta_stop");
      break;
    default:
      break;
  }
}
}  // namespace

void wifiDiagnosticsBegin() {
  offlineSinceMs = millis();
  eventQueue = xQueueCreate(kEventQueueSize, sizeof(WifiEvent));
  if (eventQueue != nullptr) {
    eventsEnabled = WiFi.onEvent(onWifiEvent) != 0;
  }
  recordDiagnostic(eventsEnabled ? BUGLOG_INFO : BUGLOG_ERROR, 1705,
            eventsEnabled ? "wifi_event_log_ready" : "wifi_event_log_init_failed");
}

void wifiDiagnosticsPoll() {
  WifiEvent event;
  // Bounded drain avoids starving GPIO/HTTP if the radio produces an event storm.
  for (uint8_t i = 0; eventQueue != nullptr && i < 4 && xQueueReceive(eventQueue, &event, 0) == pdTRUE; ++i) {
    processEvent(event);
  }
  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - lastSampleMs) >= kSignalSampleMs) {
    lastSampleMs = now;
    sampleSignal();
  }
  const uint32_t drops = droppedEvents();
  if (drops != reportedDrops && (reportedDrops == 0 || static_cast<uint32_t>(now - lastDropLogMs) >= kLogIntervalMs)) {
    char message[64];
    snprintf(message, sizeof(message), "event_queue_drops=%lu", static_cast<unsigned long>(drops));
    recordDiagnostic(BUGLOG_WARN, 1705, message);
    reportedDrops = drops;
    lastDropLogMs = now;
  }
}

void wifiDiagnosticsReconnectAttempt(bool accepted) {
  ++reconnectCalls;
  ++outageReconnectCalls;
  if (!accepted) ++reconnectFailures;
  const uint32_t now = millis();
  if (!retryLogged || static_cast<uint32_t>(now - lastRetryLogMs) >= kLogIntervalMs) {
    char message[64];
    snprintf(message, sizeof(message), "retry n=%lu ok=%u st=%d off=%lus r=%u/%u sig=%ld",
             static_cast<unsigned long>(outageReconnectCalls), accepted ? 1U : 0U, static_cast<int>(WiFi.status()),
             static_cast<unsigned long>(offline ? static_cast<uint32_t>(now - offlineSinceMs) / 1000 : 0),
             outageFirstReason, lastReason, static_cast<long>(lastRssi));
    recordDiagnostic(BUGLOG_WARN, 1703, message);
    retryLogged = true;
    lastRetryLogMs = now;
  }
}

void wifiDiagnosticsScanStart() {
  scanActive = true;
  scanStartedMs = millis();
  ++scanCount;
  lastScanOutcome = "running";
  char message[64];
  snprintf(message, sizeof(message), "scan_start n=%lu st=%d sig=%ld fh=%lu mb=%lu",
           static_cast<unsigned long>(scanCount), static_cast<int>(WiFi.status()), static_cast<long>(WiFi.RSSI()),
           static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  // Flush old entries first so the start marker cannot be rejected by a full queue.
  buglog_flush(8);
  recordDiagnostic(BUGLOG_INFO, 1710, message);
  buglog_flush(8);
}

void wifiDiagnosticsScanEnd(const char *outcome, int result) {
  scanDurationMs = static_cast<uint32_t>(millis() - scanStartedMs);
  scanActive = false;
  lastScanResult = result;
  lastScanOutcome = outcome;  // Callers pass static string literals only.
  char message[64];
  snprintf(message, sizeof(message), "scan_%s rc=%d ms=%lu fh=%lu mb=%lu",
           outcome, result, static_cast<unsigned long>(scanDurationMs),
           static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  recordDiagnostic(result >= 0 ? BUGLOG_INFO : BUGLOG_WARN, 1711, message);
  buglog_flush(8);
}

void wifiDiagnosticsScanResponse(const char *phase, uint32_t detail) {
  char message[64];
  snprintf(message, sizeof(message), "scan_reply_%s n=%lu fh=%lu mb=%lu",
           phase, static_cast<unsigned long>(detail), static_cast<unsigned long>(ESP.getFreeHeap()),
           static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  buglog_flush(8);
  recordDiagnostic(BUGLOG_INFO, 1712, message);
  buglog_flush(8);
}

void wifiDiagnosticsAppendJson(String &json) {
  const uint32_t now = millis();
  char bssid[18];
  snprintf(bssid, sizeof(bssid), "%02X:%02X:%02X:%02X:%02X:%02X",
           lastBssid[0], lastBssid[1], lastBssid[2], lastBssid[3], lastBssid[4], lastBssid[5]);
  json += "\"wifi_diag\":{\"events_enabled\":";
  json += eventsEnabled ? "true" : "false";
  json += ",\"disconnects\":" + String(disconnects);
  json += ",\"got_ip_events\":" + String(gotIpEvents);
  json += ",\"lost_ip_events\":" + String(lostIpEvents);
  json += ",\"reconnect_calls\":" + String(reconnectCalls);
  json += ",\"reconnect_call_failures\":" + String(reconnectFailures);
  json += ",\"event_drops\":" + String(droppedEvents());
  json += ",\"log_drops\":" + String(logDrops);
  json += ",\"last_reason\":" + String(lastReason);
  json += ",\"outage_first_reason\":" + String(outageFirstReason);
  json += ",\"last_reason_name\":\"" + String(reasonName()) + "\"";
  json += ",\"last_disconnect_uptime_ms\":" + String(lastDisconnectMs);
  json += ",\"last_disconnect_rssi\":" + String(lastDisconnectRssi);
  json += ",\"last_rssi\":" + String(lastRssi);
  json += ",\"last_channel\":" + String(lastChannel);
  json += ",\"last_bssid\":\"" + String(bssid) + "\"";
  json += ",\"offline_ms\":" + String(offline ? static_cast<uint32_t>(now - offlineSinceMs) : 0);
  json += ",\"last_outage_ms\":" + String(lastOutageMs);
  json += ",\"scan_active\":" + String(scanActive ? "true" : "false");
  json += ",\"scan_count\":" + String(scanCount);
  json += ",\"scan_rejected_count\":" + String(scanRejectedCount);
  json += ",\"last_scan_outcome\":\"" + String(lastScanOutcome) + "\"";
  json += ",\"last_scan_result\":" + String(lastScanResult);
  json += ",\"last_scan_duration_ms\":" + String(scanDurationMs);
  json += "},";
}

void wifiDiagnosticsScanRejected() {
  ++scanRejectedCount;
  const uint32_t now = millis();
  if (scanRejectedCount == 1 || static_cast<uint32_t>(now - lastScanRejectedLogMs) >= kLogIntervalMs) {
    char message[64];
    snprintf(message, sizeof(message), "scan_rate_limited n=%lu", static_cast<unsigned long>(scanRejectedCount));
    recordDiagnostic(BUGLOG_WARN, 1713, message);
    lastScanRejectedLogMs = now;
  }
}
