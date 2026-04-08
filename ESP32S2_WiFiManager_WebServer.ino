#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <esp_image_format.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <time.h>
#include <sys/time.h>
#include <freertos/semphr.h>
#include "buglog.h"

WebServer server(80);
WiFiManager wm;
Preferences prefs;

#define FW_VERSION "1.5.18"

// =====================
// PIN (S2 mini)
// =====================
const int STATUS_LED_PIN = LED_BUILTIN; // S2 mini: LED user (tipicamente GPIO15)
const int PC_PWR_PIN = 33;              // GPIO "pulito" e affidabile per opto
const unsigned long DEFAULT_FORCE_MS = 3000;

// =====================
// Power pulse config
// =====================
const unsigned long DEFAULT_PULSE_MS = 500;
const unsigned long LOCKOUT_MS = 8000;
const unsigned long TELEGRAM_CONFIRM_TIMEOUT_MS = 60000;
const unsigned long DEFAULT_TEST_TIMEOUT_MS = 1800000;
unsigned long pulseMs = DEFAULT_PULSE_MS;
unsigned long forceMs = DEFAULT_FORCE_MS;
unsigned long testModeTimeoutMs = DEFAULT_TEST_TIMEOUT_MS;

volatile bool pulseActive = false;
unsigned long pulseEndMs = 0;
unsigned long lastPulseMs = 0;

// Test mode for diagnostics
bool testModeEnabled = false;
bool testOutputHigh = false;
unsigned long testModeUntilMs = 0;

String lastTrigger = "boot";

String wifiSsidConfig = "";
String wifiPassConfig = "";

#if __has_include("telegram_secrets.h")
#include "telegram_secrets.h"
#endif

#if __has_include("local_services.h")
#include "local_services.h"
#endif

#ifndef TELEGRAM_BOT_TOKEN_VALUE
#define TELEGRAM_BOT_TOKEN_VALUE ""
#endif

#ifndef TELEGRAM_CHAT_ID_VALUE
#define TELEGRAM_CHAT_ID_VALUE ""
#endif

#ifndef TELEGRAM_RUNTIME_ENABLED
#define TELEGRAM_RUNTIME_ENABLED 0
#endif

#ifndef NODERED_REPORT_URL_VALUE
#define NODERED_REPORT_URL_VALUE ""
#endif

// Telegram bot dedicated to this device.
// The device both sends notifications and polls getUpdates for commands.
const char *TELEGRAM_BOT_TOKEN = TELEGRAM_BOT_TOKEN_VALUE;  // Set via build flags / build_opt.h.
const char *TELEGRAM_CHAT_ID = TELEGRAM_CHAT_ID_VALUE;      // Set via build flags / build_opt.h.
const char *NODERED_REPORT_URL = NODERED_REPORT_URL_VALUE;  // Local HTTP endpoint on Node-RED / LAN service.
const bool TELEGRAM_TASK_AUTOSTART = TELEGRAM_RUNTIME_ENABLED;
static volatile int tgLastHttpCode = 0;  // volatile: scritto da telegramTask, letto da loop
bool telegramNotifyPending = false;
unsigned long telegramNextTryMs = 0;
const unsigned long TELEGRAM_RETRY_MS = 15000;
const int TELEGRAM_LONG_POLL_SEC = 55;
const bool TELEGRAM_POLL_VERBOSE_LOG = false;  // true only for diagnostics
const uint32_t TELEGRAM_LONG_POLL_GRACE_MS = 4000;
const uint32_t TELEGRAM_HTTP_CONNECT_TIMEOUT_MS = 5000;
const uint32_t TELEGRAM_SEND_TIMEOUT_MS = 4500;
const uint32_t TELEGRAM_SHORT_HTTP_TIMEOUT_MS = 5000;
const uint32_t TELEGRAM_WEBHOOK_HTTP_TIMEOUT_MS = 6500;
const uint8_t TELEGRAM_POLL_RESULT_LIMIT = 1;
const unsigned long TELEGRAM_TASK_RETRY_MS = 1500;
const unsigned long TELEGRAM_LOW_HEAP_PAUSE_MS = 300000;
const uint32_t TELEGRAM_MIN_MAX_BLOCK_BYTES = 24000;
const uint32_t TELEGRAM_HEAP_RECOVERY_MAX_BLOCK_BYTES = 16000;
const uint32_t TELEGRAM_HEAP_RECOVERY_FREE_HEAP_BYTES = 22000;
const unsigned long TELEGRAM_HEAP_RECOVERY_RESTART_COOLDOWN_MS = 15000;
const unsigned long TELEGRAM_HEAP_RECOVERY_PAUSE_MS = 5000;
const uint32_t TELEGRAM_SOFT_RECYCLE_MAX_BLOCK_BYTES = 42000;
const uint8_t TELEGRAM_SOFT_RECYCLE_POLL_OK_LIMIT = 3;
const unsigned long TELEGRAM_SOFT_RECYCLE_TASK_AGE_MS = 4UL * 60UL * 1000UL;
const unsigned long TELEGRAM_SOFT_RECYCLE_PAUSE_MS = 4000;
const bool TELEGRAM_BOOT_NOTIFY_ENABLED = false;
const bool TELEGRAM_AUTO_WEBHOOK_PRIME_ENABLED = false;
const bool TELEGRAM_AUTO_RECOVERY_FOLLOWUP_ENABLED = false;
const unsigned long TELEGRAM_ERROR_BACKOFF_BASE_MS = 1200;
const unsigned long TELEGRAM_ERROR_BACKOFF_MAX_MS = 20000;
const uint8_t TELEGRAM_MAX_CONSECUTIVE_ERRORS = 8;
const unsigned long TELEGRAM_FAILSAFE_PAUSE_MS = 120000;
const uint8_t TELEGRAM_SEND_ATTEMPTS = 2;
const unsigned long TELEGRAM_SEND_RETRY_GAP_MS = 180;
const unsigned long TELEGRAM_SEND_TOTAL_BUDGET_MS = 12000;
const unsigned long TELEGRAM_WEBHOOK_RECOVERY_COOLDOWN_MS = 30000;
const unsigned long TELEGRAM_OFFSET_SAVE_INTERVAL_MS = 10000;
const uint8_t TELEGRAM_BOOT_NOTIFY_RETRY_MAX = 3;
const bool TELEGRAM_NET_DIAG_VERBOSE = false;
const unsigned long TELEGRAM_CMD_MIN_GAP_MS = 1200;
const unsigned long TELEGRAM_CMD_WINDOW_MS = 10000;
const uint16_t TELEGRAM_CMD_MAX_HITS = 4;
const unsigned long TELEGRAM_CMD_THROTTLE_MS = 30000;
const unsigned long TELEGRAM_TASK_STALE_RESTART_MS = 125000;
const unsigned long TELEGRAM_TASK_RESTART_COOLDOWN_MS = 180000;
const size_t TELEGRAM_API_PATH_CAPACITY = 192;
const size_t TELEGRAM_API_DESC_CAPACITY = 96;
const size_t TELEGRAM_SMALL_JSON_CAPACITY = 256;
const size_t TELEGRAM_SEND_JSON_CAPACITY = 1024;
const size_t TELEGRAM_SEND_BODY_CAPACITY = 1280;
const size_t TELEGRAM_SMALL_BODY_CAPACITY = 128;
const size_t TELEGRAM_POLL_FILTER_JSON_CAPACITY = 256;
const size_t TELEGRAM_POLL_JSON_CAPACITY = 3072;
const unsigned long WATCHDOG_CHECK_EVERY_MS = 1000;
const unsigned long WATCHDOG_LOOP_STALL_MS = 45000;
const unsigned long OTA_STALL_REBOOT_MS = 120000;
const unsigned long BUGLOG_HEARTBEAT_NORMAL_MS = 15UL * 60UL * 1000UL;
const unsigned long BUGLOG_HEARTBEAT_ALERT_MS = 2UL * 60UL * 1000UL;
const unsigned long BUGLOG_HEARTBEAT_STALE_TASK_MS = 10000UL;
const uint32_t BUGLOG_HEARTBEAT_LOW_FREE_HEAP_BYTES = 50000UL;
const unsigned long WIFI_OFFLINE_TO_AP_MS = 5000;
const unsigned long NODERED_REPORT_RETRY_MS = 15000UL;
const unsigned long NODERED_REPORT_BOOT_DELAY_MS = 1500UL;
const uint32_t NODERED_REPORT_TIMEOUT_MS = 2500UL;
const char *FALLBACK_AP_SSID = "ESP32S2-Setup";
const char *OTA_UPLOAD_HEADERS[] = {"X-File-Size"};
const size_t OTA_UPLOAD_HEADERS_COUNT = 1;
const unsigned long WEB_SENSITIVE_WINDOW_MS = 15000;
const unsigned long WEB_SENSITIVE_MIN_GAP_MS = 900;
const uint16_t WEB_SENSITIVE_MAX_HITS = 10;
const unsigned long WEB_WIFI_SCAN_WINDOW_MS = 15000;
const unsigned long WEB_WIFI_SCAN_MIN_GAP_MS = 2500;
const uint16_t WEB_WIFI_SCAN_MAX_HITS = 4;
const unsigned long WEB_OTA_WINDOW_MS = 30000;
const unsigned long WEB_OTA_MIN_GAP_MS = 4000;
const uint16_t WEB_OTA_MAX_HITS = 4;
long telegramLastUpdateId = 0;
long telegramConsoleMessageId = 0;
long telegramPendingConfirmMessageId = 0;
uint32_t telegramPendingConfirmNonce = 0;
String telegramPendingConfirmAction = "";
unsigned long telegramPendingConfirmExpiresMs = 0;
TaskHandle_t telegramTaskHandle = nullptr;
TaskHandle_t safetyTaskHandle = nullptr;
SemaphoreHandle_t telegramHttpMutex = nullptr;
SemaphoreHandle_t logMutex = nullptr;
SemaphoreHandle_t prefsMutex = nullptr;
volatile bool telegramHttpInFlight = false;
// BUG-1 FIX: char array + portENTER_CRITICAL invece di String
// (String usa heap con new/delete internamente; su S2 una preemption durante operator=
//  pu?? corrompere il puntatore se letto contemporaneamente dal loop task via statusJson)
char tgLastErrorDescriptionBuf[64] = "";
unsigned long telegramLastReceiveMs = 0;
unsigned long telegramLastSendOkMs = 0;
unsigned long telegramLastPollOkMs = 0;
unsigned long telegramLastWebhookRecoveryMs = 0;
unsigned long telegramWebhookRecoveryNotBeforeMs = 0;
unsigned long telegramLastOffsetSaveMs = 0;
long telegramLastOffsetSaved = 0;
unsigned long telegramCmdLastMs = 0;
unsigned long telegramCmdWindowStartMs = 0;
uint16_t telegramCmdWindowHits = 0;
unsigned long telegramCmdThrottleUntilMs = 0;
unsigned long telegramRateLimitLastLogMs = 0;
bool telegramWebhookRecoveryPending = false;
char telegramWebhookRecoveryReason[32] = "";
volatile uint32_t telegramMetricUpdatesRx = 0;
volatile uint32_t telegramMetricCommandsRx = 0;
volatile uint32_t telegramMetricSendOk = 0;
volatile uint32_t telegramMetricSendKo = 0;
volatile uint32_t telegramMetricPollOk = 0;
volatile uint32_t telegramMetricPollKo = 0;
volatile uint32_t telegramMetricPollParseKo = 0;
volatile uint32_t telegramMetricWebhookRecoveries = 0;
volatile uint32_t telegramMetricCmdRateDrops = 0;

// =====================
// Device command queue
// =====================
enum DeviceCmdType : uint8_t {
  DCMD_PULSE,
  DCMD_FORCE,
  DCMD_TEST_ON,
  DCMD_TEST_OFF,
  DCMD_REBOOT,
};

struct DeviceCmd {
  DeviceCmdType type;
  unsigned long durationMs;
  char source[28];
};

QueueHandle_t deviceCmdQueue = nullptr;

static const uint8_t RATE_LIMIT_SLOTS = 4;

struct WebRateLimitEntry {
  IPAddress ip;
  unsigned long windowStartMs = 0;
  unsigned long lastHitMs = 0;
  uint16_t hits = 0;
};

struct WebRateLimitBucket {
  WebRateLimitEntry slots[RATE_LIMIT_SLOTS];
};


const char *ROME_TZ = "CET-1CEST,M3.5.0/2,M10.5.0/3";
const char *NTP_SERVER_1 = "pool.ntp.org";
const char *NTP_SERVER_2 = "time.google.com";
const unsigned long TIME_SYNC_TIMEOUT_MS = 5000;
bool timeConfigured = false;
esp_reset_reason_t bootResetReason = ESP_RST_UNKNOWN;
uint32_t bootSessionId = 0;
bool lastWiFiConnected = false;

// =====================
// Safe mode (crash recovery)
// =====================
RTC_DATA_ATTR uint8_t rtcCrashCount = 0;   // sopravvive ai reboot SW, azzerato al power-on
RTC_DATA_ATTR uint8_t rtcSoftLockRebootCount = 0;  // reboot forzati da watchdog (soft-lock)
RTC_DATA_ATTR uint8_t rtcIntentionalReboot = 0;    // flag reboot richiesto da logica applicativa
const uint8_t SAFE_MODE_CRASH_THRESHOLD = 3;
const uint8_t FACTORY_BOOT_CRASH_THRESHOLD = 6;  // dopo 6 crash salta alla factory
const unsigned long SAFE_MODE_CLEAR_MS = 60000;  // dopo 60s stabili ??? reset contatore
const uint8_t SOFTLOCK_FACTORY_BOOT_THRESHOLD = 3;
const unsigned long SOFTLOCK_CLEAR_MS = 120000;
const uint8_t BOOT_HISTORY_CAPACITY = 12;
bool safeModeActive = false;
unsigned long safeModeClearAtMs = 0;
unsigned long softLockClearAtMs = 0;
const int WEB_LOG_CAPACITY = 100;
String webLogBuffer[WEB_LOG_CAPACITY];
int webLogHead = 0;
int webLogCount = 0;
volatile bool otaUploadInProgress = false;
volatile bool otaUploadFinished = false;
volatile bool otaUploadSuccess = false;
uint32_t otaUploadExpectedBytes = 0;
uint32_t otaUploadWrittenBytes = 0;
uint8_t otaHeaderProbe[sizeof(esp_image_header_t)] = {0};
size_t otaHeaderProbeLen = 0;
bool otaHeaderValidated = false;
String otaUploadStatus = "idle";
unsigned long otaUploadStartedMs = 0;
unsigned long otaLastChunkMs = 0;
volatile bool otaRebootScheduled = false;
unsigned long otaRebootAtMs = 0;
volatile bool otaStallRecoveryRequested = false;
bool fallbackApActive = false;
unsigned long wifiOfflineSinceMs = 0;
const esp_partition_t *otaTargetPartition = nullptr;
bool wifiConfigNeedsSync = false;
bool telegramBootNotifyAttempted = false;
uint8_t telegramBootNotifyAttempts = 0;
unsigned long bootMillisBaseline = 0;
unsigned long telegramBackoffUntilMs = 0;
unsigned long telegramFailsafeUntilMs = 0;
uint8_t telegramConsecutiveErrors = 0;
volatile unsigned long loopHeartbeatMs = 0;
volatile unsigned long telegramHeartbeatMs = 0;
unsigned long telegramTaskLastRestartMs = 0;
volatile uint32_t telegramTaskRestartCount = 0;
volatile bool telegramHeapRecoveryRequested = false;
volatile uint32_t telegramHeapRecoveryFreeHeap = 0;
volatile uint32_t telegramHeapRecoveryMaxBlock = 0;
unsigned long telegramHeapRecoveryLastLogMs = 0;
uint8_t telegramPollOkSinceRestart = 0;
char telegramHeapRecoveryReasonBuf[24] = "heap_guard";
uint16_t telegramHeapRecoveryReasonCode = 1313;
WebRateLimitBucket webSensitiveBucket;
WebRateLimitBucket webWifiScanBucket;
WebRateLimitBucket webOtaBucket;
char telegramNotifyReasonBuf[24] = "boot";
bool nodeRedReportPending = false;
unsigned long nodeRedReportDueMs = 0;
char nodeRedReportReasonBuf[24] = "";
portMUX_TYPE telegramStateMux = portMUX_INITIALIZER_UNLOCKED;

// =====================
// WiFi tuning
// =====================
const int WIFI_CONNECT_TIMEOUT_SEC = 30;         // ? aumentato a 30s
const unsigned long WIFI_RECONNECT_EVERY_MS = 5000;
const unsigned long WIFI_SCAN_TIMEOUT_MS = 15000;

// LED blink when offline
const unsigned long LED_BLINK_MS = 350;
unsigned long ledNextToggleMs = 0;
bool ledBlinkState = false;
bool wifiScanPending = false;
unsigned long wifiScanStartedMs = 0;
unsigned long buglogHeartbeatNextMs = 0;
const unsigned long HEAP_PROBE_LOG_COOLDOWN_MS = 10UL * 60UL * 1000UL;
const uint32_t HEAP_PROBE_DROP_FREE_BYTES = 8192UL;
const uint32_t HEAP_PROBE_DROP_MAX_BLOCK_BYTES = 4096UL;
const uint32_t HEAP_PROBE_NEW_LOW_STEP_BYTES = 2048UL;
const uint32_t HEAP_PROBE_CRITICAL_FREE_BYTES = 24576UL;
const uint32_t HEAP_PROBE_CRITICAL_MAX_BLOCK_BYTES = 12288UL;

struct HeapSnapshot {
  uint32_t freeHeap = 0;
  uint32_t maxBlock = 0;
};

struct HeapProbeState {
  uint32_t minFreeHeap = 0;
  uint32_t minMaxBlock = 0;
  unsigned long lastLogMs = 0;
};

HeapProbeState heapProbeTelegramPoll;
HeapProbeState heapProbeTelegramSend;
HeapProbeState heapProbeTelegramRecovery;
HeapProbeState heapProbeStatusJson;
HeapProbeState heapProbeWebLogs;
HeapProbeState heapProbeWifiScan;
HeapProbeState heapProbeTelegramStatusMsg;
HeapProbeState heapProbeBootConfig;
HeapProbeState heapProbeBootWifi;
HeapProbeState heapProbeBootNtp;
HeapProbeState heapProbeBootHttp;
HeapProbeState heapProbeBootTasks;

// =====================
// Helpers
// =====================
int rssiToPercent(int rssi) {
  if (rssi <= -100) return 0;
  if (rssi >= -50) return 100;
  return 2 * (rssi + 100);
}

bool wifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

bool bootToFactory() {
  const esp_partition_t *factory = esp_partition_find_first(
    ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, nullptr);
  if (factory == nullptr) return false;
  esp_err_t err = esp_ota_set_boot_partition(factory);
  return (err == ESP_OK);
}

void markIntentionalReboot() {
  rtcIntentionalReboot = 1;
}

void restartFromWatchdog(const String &reason) {
  logLine("[SAFE] Watchdog reboot: " + reason);
  log_event(BUGLOG_FATAL, 1101, reason);
  buglog_flush(8);
  if (rtcSoftLockRebootCount < 255) rtcSoftLockRebootCount++;
  rtcIntentionalReboot = 0;
  logLine("[SAFE] softlock_reboot_count=" + String(rtcSoftLockRebootCount));
  appendPersistentBootEvent("watchdog reason=" + reason + " soft=" + String(rtcSoftLockRebootCount));

  if (rtcSoftLockRebootCount >= SOFTLOCK_FACTORY_BOOT_THRESHOLD) {
    logLine("[SAFE] Soglia soft-lock raggiunta, provo boot in recovery factory");
    if (bootToFactory()) {
      rtcSoftLockRebootCount = 0;
      rtcCrashCount = 0;
      delay(200);
      ESP.restart();
      return;
    }
    logLine("[SAFE] Factory partition non trovata, reboot normale");
  }

  delay(100);
  ESP.restart();
}

bool bootToFactoryNow(const String &reason) {
  logLine("[SAFE] Auto recovery -> factory: " + reason);
  log_event(BUGLOG_ERROR, 1102, reason);
  buglog_flush(8);
  appendPersistentBootEvent("boot_factory reason=" + reason + " reset=" + String((int)bootResetReason));
  if (!bootToFactory()) {
    logLine("[SAFE] Auto recovery fallita: factory partition non trovata");
    return false;
  }
  rtcCrashCount = 0;
  rtcSoftLockRebootCount = 0;
  markIntentionalReboot();
  delay(200);
  ESP.restart();
  return true;
}

bool outputIsHigh() {
  return pulseActive || (testModeEnabled && testOutputHigh);
}

bool telegramConfigured() {
  return TELEGRAM_RUNTIME_ENABLED &&
         strlen(TELEGRAM_BOT_TOKEN) > 0 &&
         strlen(TELEGRAM_CHAT_ID) > 0;
}

bool nodeRedReportConfigured() {
  return strlen(NODERED_REPORT_URL) > 0;
}

String bootSessionIdHex() {
  String out = String(bootSessionId, HEX);
  out.toUpperCase();
  return out;
}

bool lockLogs(TickType_t timeoutTicks = pdMS_TO_TICKS(120)) {
  if (logMutex == nullptr) return true;
  return xSemaphoreTake(logMutex, timeoutTicks) == pdTRUE;
}

void unlockLogs() {
  if (logMutex != nullptr) xSemaphoreGive(logMutex);
}

HeapSnapshot captureHeapSnapshot() {
  HeapSnapshot snap;
  snap.freeHeap = ESP.getFreeHeap();
  snap.maxBlock = ESP.getMaxAllocHeap();
  return snap;
}

void maybeLogHeapProbe(const char *tag, uint16_t code, HeapProbeState &state, const HeapSnapshot &before) {
  HeapSnapshot after = captureHeapSnapshot();
  bool firstSample = (state.minFreeHeap == 0 || state.minMaxBlock == 0);
  if (firstSample) {
    state.minFreeHeap = before.freeHeap;
    state.minMaxBlock = before.maxBlock;
  }

  bool freeDrop = before.freeHeap > after.freeHeap &&
                  (before.freeHeap - after.freeHeap) >= HEAP_PROBE_DROP_FREE_BYTES;
  bool maxDrop = before.maxBlock > after.maxBlock &&
                 (before.maxBlock - after.maxBlock) >= HEAP_PROBE_DROP_MAX_BLOCK_BYTES;
  bool newLowFree = after.freeHeap + HEAP_PROBE_NEW_LOW_STEP_BYTES <= state.minFreeHeap;
  bool newLowMax = after.maxBlock + HEAP_PROBE_NEW_LOW_STEP_BYTES <= state.minMaxBlock;
  bool critical = after.freeHeap < HEAP_PROBE_CRITICAL_FREE_BYTES ||
                  after.maxBlock < HEAP_PROBE_CRITICAL_MAX_BLOCK_BYTES;
  bool forceLog = freeDrop || maxDrop || newLowFree || newLowMax;

  if (after.freeHeap < state.minFreeHeap) state.minFreeHeap = after.freeHeap;
  if (after.maxBlock < state.minMaxBlock) state.minMaxBlock = after.maxBlock;

  if (!(freeDrop || maxDrop || newLowFree || newLowMax || critical)) return;

  unsigned long now = millis();
  if (state.lastLogMs != 0 &&
      (long)(now - state.lastLogMs) < (long)HEAP_PROBE_LOG_COOLDOWN_MS &&
      !forceLog) {
    return;
  }

  char msg[64];
  snprintf(
    msg,
    sizeof(msg),
    "%s %lu/%lu>%lu/%lu",
    tag,
    (unsigned long)before.freeHeap,
    (unsigned long)before.maxBlock,
    (unsigned long)after.freeHeap,
    (unsigned long)after.maxBlock
  );
  log_event(BUGLOG_WARN, code, msg);
  state.lastLogMs = now;
}

class ScopedHeapProbe {
public:
  ScopedHeapProbe(const char *tag, uint16_t code, HeapProbeState &state)
      : tag_(tag), code_(code), state_(state), before_(captureHeapSnapshot()) {}

  ~ScopedHeapProbe() {
    maybeLogHeapProbe(tag_, code_, state_, before_);
  }

private:
  const char *tag_;
  uint16_t code_;
  HeapProbeState &state_;
  HeapSnapshot before_;
};

void logBootStageSnapshot(const char *tag, uint16_t code) {
  HeapSnapshot snap = captureHeapSnapshot();
  char msg[64];
  snprintf(
    msg,
    sizeof(msg),
    "%s fh=%lu mb=%lu",
    tag,
    (unsigned long)snap.freeHeap,
    (unsigned long)snap.maxBlock
  );
  bool anomaly = snap.freeHeap < BUGLOG_HEARTBEAT_LOW_FREE_HEAP_BYTES ||
                 snap.maxBlock < TELEGRAM_MIN_MAX_BLOCK_BYTES;
  log_event(anomaly ? BUGLOG_WARN : BUGLOG_INFO, code, msg);
}

void maybeLogTelegramStageSnapshot(const char *tag, uint16_t code, uint32_t ordinal) {
  HeapSnapshot snap = captureHeapSnapshot();
  bool anomaly = snap.freeHeap < BUGLOG_HEARTBEAT_LOW_FREE_HEAP_BYTES ||
                 snap.maxBlock < TELEGRAM_MIN_MAX_BLOCK_BYTES;
  if (!anomaly && ordinal > 2U) return;

  char msg[64];
  snprintf(
    msg,
    sizeof(msg),
    "%s n=%lu fh=%lu mb=%lu",
    tag,
    (unsigned long)ordinal,
    (unsigned long)snap.freeHeap,
    (unsigned long)snap.maxBlock
  );
  log_event(anomaly ? BUGLOG_WARN : BUGLOG_INFO, code, msg);
}

class ScopedTelegramHttpInFlight {
public:
  ScopedTelegramHttpInFlight() { telegramHttpInFlight = true; }
  ~ScopedTelegramHttpInFlight() { telegramHttpInFlight = false; }
};

bool debugSerialReady() {
#if ARDUINO_USB_CDC_ON_BOOT
  return static_cast<bool>(Serial);
#else
  return true;
#endif
}

void debugPrintLine(const String &line) {
  if (!debugSerialReady()) return;
  Serial.println(line);
}

void debugPrintUpdateError() {
  if (!debugSerialReady()) return;
  Update.printError(Serial);
}

void copyReasonString(char *dest, size_t destSize, const String &src) {
  if (dest == nullptr || destSize == 0) return;
  size_t len = src.length();
  if (len >= destSize) len = destSize - 1;
  memcpy(dest, src.c_str(), len);
  dest[len] = '\0';
}

String telegramNotifyReasonSnapshot() {
  char local[sizeof(telegramNotifyReasonBuf)];
  portENTER_CRITICAL(&telegramStateMux);
  memcpy(local, telegramNotifyReasonBuf, sizeof(local));
  portEXIT_CRITICAL(&telegramStateMux);
  local[sizeof(local) - 1] = '\0';
  return String(local);
}

String nodeRedReportReasonSnapshot() {
  char local[sizeof(nodeRedReportReasonBuf)];
  portENTER_CRITICAL(&telegramStateMux);
  memcpy(local, nodeRedReportReasonBuf, sizeof(local));
  portEXIT_CRITICAL(&telegramStateMux);
  local[sizeof(local) - 1] = '\0';
  return String(local);
}

void setTelegramNotifyState(const String &reason, unsigned long dueMs) {
  portENTER_CRITICAL(&telegramStateMux);
  telegramNotifyPending = true;
  copyReasonString(telegramNotifyReasonBuf, sizeof(telegramNotifyReasonBuf), reason);
  telegramNextTryMs = dueMs;
  portEXIT_CRITICAL(&telegramStateMux);
}

void setNodeRedReportState(const String &reason, unsigned long dueMs) {
  portENTER_CRITICAL(&telegramStateMux);
  nodeRedReportPending = true;
  copyReasonString(nodeRedReportReasonBuf, sizeof(nodeRedReportReasonBuf), reason);
  nodeRedReportDueMs = dueMs;
  portEXIT_CRITICAL(&telegramStateMux);
}

void clearTelegramNotifyPending() {
  portENTER_CRITICAL(&telegramStateMux);
  telegramNotifyPending = false;
  portEXIT_CRITICAL(&telegramStateMux);
}

void clearNodeRedReportState() {
  portENTER_CRITICAL(&telegramStateMux);
  nodeRedReportPending = false;
  nodeRedReportReasonBuf[0] = '\0';
  nodeRedReportDueMs = 0;
  portEXIT_CRITICAL(&telegramStateMux);
}

String telegramWebhookRecoveryReasonSnapshot() {
  char local[sizeof(telegramWebhookRecoveryReason)];
  portENTER_CRITICAL(&telegramStateMux);
  memcpy(local, telegramWebhookRecoveryReason, sizeof(local));
  portEXIT_CRITICAL(&telegramStateMux);
  local[sizeof(local) - 1] = '\0';
  return String(local);
}

String telegramHeapRecoveryReasonSnapshot() {
  char local[sizeof(telegramHeapRecoveryReasonBuf)];
  portENTER_CRITICAL(&telegramStateMux);
  memcpy(local, telegramHeapRecoveryReasonBuf, sizeof(local));
  portEXIT_CRITICAL(&telegramStateMux);
  local[sizeof(local) - 1] = '\0';
  return String(local);
}

uint16_t telegramHeapRecoveryCodeSnapshot() {
  uint16_t code = 1313;
  portENTER_CRITICAL(&telegramStateMux);
  code = telegramHeapRecoveryReasonCode;
  portEXIT_CRITICAL(&telegramStateMux);
  return code;
}

void setTelegramWebhookRecoveryState(const String &reason, unsigned long notBeforeMs) {
  portENTER_CRITICAL(&telegramStateMux);
  telegramWebhookRecoveryPending = true;
  copyReasonString(telegramWebhookRecoveryReason, sizeof(telegramWebhookRecoveryReason), reason);
  telegramWebhookRecoveryNotBeforeMs = notBeforeMs;
  portEXIT_CRITICAL(&telegramStateMux);
}

void clearTelegramWebhookRecoveryState() {
  portENTER_CRITICAL(&telegramStateMux);
  telegramWebhookRecoveryPending = false;
  telegramWebhookRecoveryReason[0] = '\0';
  telegramWebhookRecoveryNotBeforeMs = 0;
  portEXIT_CRITICAL(&telegramStateMux);
}

// Helpers per tgLastErrorDescriptionBuf (BUG-1)
// setTgLastError ?? safe da qualsiasi task; getTgLastError restituisce uno snapshot.
void setTgLastError(const char *err) {
  portENTER_CRITICAL(&telegramStateMux);
  if (err == nullptr) {
    tgLastErrorDescriptionBuf[0] = '\0';
  } else {
    size_t i = 0;
    for (; i + 1 < sizeof(tgLastErrorDescriptionBuf) && err[i] != '\0'; ++i)
      tgLastErrorDescriptionBuf[i] = err[i];
    tgLastErrorDescriptionBuf[i] = '\0';
  }
  portEXIT_CRITICAL(&telegramStateMux);
}

String getTgLastError() {
  char snap[sizeof(tgLastErrorDescriptionBuf)];
  portENTER_CRITICAL(&telegramStateMux);
  memcpy(snap, tgLastErrorDescriptionBuf, sizeof(snap));
  portEXIT_CRITICAL(&telegramStateMux);
  snap[sizeof(snap) - 1] = '\0';
  return String(snap);
}

void appendWebLog(const String &line) {
  if (!lockLogs()) return;
  unsigned long nowMs = millis();
  unsigned long sinceBootMs = (bootMillisBaseline == 0) ? nowMs : (nowMs - bootMillisBaseline);
  String ts = timeConfigured
    ? formatLocalTimeRome()
    : ("UNSYNCED+" + String(sinceBootMs) + "ms");
  String entry = ts + " " + line;
  webLogBuffer[webLogHead] = entry;
  webLogHead = (webLogHead + 1) % WEB_LOG_CAPACITY;
  if (webLogCount < WEB_LOG_CAPACITY) webLogCount++;
  unlockLogs();
}

void logLine(const String &line) {
  debugPrintLine(line);
  appendWebLog(line);
}

void maybeLogPersistentHeartbeat() {
  unsigned long now = millis();
  if (buglogHeartbeatNextMs != 0 && (long)(now - buglogHeartbeatNextMs) < 0) return;

  unsigned long loopAgeMs = loopHeartbeatMs > 0 ? (now - loopHeartbeatMs) : 0;
  unsigned long telegramAgeMs = telegramHeartbeatMs > 0 ? (now - telegramHeartbeatMs) : 0;
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t maxBlock = ESP.getMaxAllocHeap();
  bool telegramStale = telegramTaskHandle != nullptr && telegramAgeMs > BUGLOG_HEARTBEAT_STALE_TASK_MS;
  bool telegramBusy = telegramHttpInFlight;
  bool anomaly = !wifiConnected() ||
                 (!telegramBusy && freeHeap < BUGLOG_HEARTBEAT_LOW_FREE_HEAP_BYTES) ||
                 (!telegramBusy && maxBlock < TELEGRAM_MIN_MAX_BLOCK_BYTES) ||
                 telegramStale;

  char msg[64];
  snprintf(
    msg,
    sizeof(msg),
    "fh=%lu mb=%lu wf=%u la=%lu ta=%lu",
    (unsigned long)freeHeap,
    (unsigned long)maxBlock,
    wifiConnected() ? 1U : 0U,
    loopAgeMs,
    telegramAgeMs
  );

  log_event(anomaly ? BUGLOG_WARN : BUGLOG_INFO, anomaly ? 1501 : 1500, msg);
  buglogHeartbeatNextMs = now + (anomaly ? BUGLOG_HEARTBEAT_ALERT_MS : BUGLOG_HEARTBEAT_NORMAL_MS);
}

void clearWebLogs() {
  if (!lockLogs()) return;
  webLogHead = 0;
  webLogCount = 0;
  for (int i = 0; i < WEB_LOG_CAPACITY; i++) webLogBuffer[i] = "";
  unlockLogs();
}

String webLogsJson(int limit = 30) {
  ScopedHeapProbe heapProbe("web_log", 1604, heapProbeWebLogs);
  if (limit <= 0) limit = 1;
  if (limit > WEB_LOG_CAPACITY) limit = WEB_LOG_CAPACITY;
  if (!lockLogs()) return "{\"ok\":false,\"error\":\"log_busy\"}";
  int count = webLogCount < limit ? webLogCount : limit;
  int start = webLogHead - count;
  if (start < 0) start += WEB_LOG_CAPACITY;

  String json = "{\"ok\":true,\"count\":" + String(webLogCount) + ",\"shown\":" + String(count) + ",\"logs\":[";
  for (int i = 0; i < count; i++) {
    if (i > 0) json += ",";
    int idx = (start + i) % WEB_LOG_CAPACITY;
    json += "\"" + jsonEscape(webLogBuffer[idx]) + "\"";
  }
  json += "]}";
  unlockLogs();
  return json;
}

void resetOtaUploadProgress() {
  otaUploadInProgress = false;
  otaUploadFinished = false;
  otaUploadSuccess = false;
  otaUploadExpectedBytes = 0;
  otaUploadWrittenBytes = 0;
  otaHeaderProbeLen = 0;
  otaHeaderValidated = false;
  otaUploadStatus = "idle";
  otaUploadStartedMs = 0;
  otaLastChunkMs = 0;
  otaRebootScheduled = false;
  otaRebootAtMs = 0;
  otaStallRecoveryRequested = false;
  otaTargetPartition = nullptr;
}

String otaProgressJson() {
  String json;
  json.reserve(192);
  int pct = -1;
  if (otaUploadExpectedBytes > 0) {
    uint32_t clamped = otaUploadWrittenBytes > otaUploadExpectedBytes
      ? otaUploadExpectedBytes
      : otaUploadWrittenBytes;
    pct = (int)((clamped * 100ULL) / otaUploadExpectedBytes);
  } else if (otaUploadFinished && otaUploadSuccess) {
    pct = 100;
  } else if (otaUploadFinished && !otaUploadSuccess) {
    pct = 0;
  }

  json = "{";
  json += "\"ok\":true,";
  json += "\"in_progress\":" + String(otaUploadInProgress ? "true" : "false") + ",";
  json += "\"finished\":" + String(otaUploadFinished ? "true" : "false") + ",";
  json += "\"success\":" + String(otaUploadSuccess ? "true" : "false") + ",";
  json += "\"written\":" + String(otaUploadWrittenBytes) + ",";
  json += "\"expected\":" + String(otaUploadExpectedBytes) + ",";
  json += "\"pct\":" + String(pct) + ",";
  json += "\"status\":\"" + jsonEscape(otaUploadStatus) + "\",";
  json += "\"elapsed_ms\":" + String(otaUploadStartedMs == 0 ? 0 : (millis() - otaUploadStartedMs));
  json += "}";
  return json;
}

String chipModelString() {
  return String(ESP.getChipModel());
}

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

const char *resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_UNKNOWN: return "Unknown";
    case ESP_RST_POWERON: return "PowerOn";
    case ESP_RST_EXT: return "ExternalPin";
    case ESP_RST_SW: return "Software";
    case ESP_RST_PANIC: return "Crash";
    case ESP_RST_INT_WDT: return "InterruptWDT";
    case ESP_RST_TASK_WDT: return "TaskWDT";
    case ESP_RST_WDT: return "OtherWDT";
    case ESP_RST_DEEPSLEEP: return "DeepSleep";
    case ESP_RST_BROWNOUT: return "Brownout";
    case ESP_RST_SDIO: return "SDIO";
    case ESP_RST_USB: return "USB";
    case ESP_RST_JTAG: return "JTAG";
    case ESP_RST_EFUSE: return "eFuse";
    case ESP_RST_PWR_GLITCH: return "PowerGlitch";
    case ESP_RST_CPU_LOCKUP: return "CPULockup";
    default: return "Other";
  }
}

bool ensureTimeSynced(unsigned long timeoutMs) {
  if (timeConfigured) return true;

  configTzTime(ROME_TZ, NTP_SERVER_1, NTP_SERVER_2);
  unsigned long start = millis();
  struct tm tmNow;
  while (millis() - start < timeoutMs) {
    if (getLocalTime(&tmNow, 250)) {
      timeConfigured = true;
      return true;
    }
    delay(50);
  }
  return false;
}

String formatLocalTimeRome() {
  struct timeval tv;
  if (gettimeofday(&tv, nullptr) != 0) return "unavailable";

  time_t nowSec = tv.tv_sec;
  struct tm tmNow;
  localtime_r(&nowSec, &tmNow);

  char buf[48];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmNow);

  char out[64];
  snprintf(out, sizeof(out), "%s.%03ld %s", buf, tv.tv_usec / 1000, tmNow.tm_isdst ? "CEST" : "CET");
  return String(out);
}

String wifiBssidString() {
  uint8_t *bssid = WiFi.BSSID();
  if (bssid == nullptr) return "unknown";

  char buf[18];
  snprintf(
    buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]
  );
  return String(buf);
}

String runningPartitionLabel() {
  const esp_partition_t *part = esp_ota_get_running_partition();
  if (part == nullptr) return "unknown";
  return String(part->label);
}

String firmwareVersionString() {
  return String(FW_VERSION) + " (" + __DATE__ + " " + __TIME__ + ")";
}

unsigned long uptimeSeconds() {
  unsigned long now = millis();
  if (bootMillisBaseline == 0) return now / 1000UL;
  return (now - bootMillisBaseline) / 1000UL;
}

String uptimeHuman() {
  unsigned long total = uptimeSeconds();
  unsigned long totalDays = total / 86400UL;
  unsigned long years = total / 31536000UL;
  total %= 31536000UL;
  unsigned long months = total / 2592000UL;
  total %= 2592000UL;
  unsigned long days = total / 86400UL;
  total %= 86400UL;
  unsigned long hours = total / 3600UL;
  total %= 3600UL;
  unsigned long minutes = total / 60UL;
  unsigned long seconds = total % 60UL;

  String out = "";
  out += String(years) + (years == 1 ? " anno, " : " anni, ");
  out += String(months) + (months == 1 ? " mese, " : " mesi, ");
  out += String(days) + (days == 1 ? " giorno, " : " giorni, ");
  out += String(hours) + (hours == 1 ? " ora, " : " ore, ");
  out += String(minutes) + (minutes == 1 ? " minuto, " : " minuti, ");
  out += String(seconds) + (seconds == 1 ? " secondo" : " secondi");

  if (totalDays > 0) {
    out += " (ossia " + String(totalDays) + (totalDays == 1 ? " giorno" : " giorni");
    out += ", " + String(hours) + (hours == 1 ? " ora, " : " ore, ");
    out += String(minutes) + (minutes == 1 ? " minuto, " : " minuti, ");
    out += String(seconds) + (seconds == 1 ? " secondo)" : " secondi)");
  }

  return out;
}


bool havePersistentWifiConfig() {
  return wifiSsidConfig.length() > 0 || wm.getWiFiIsSaved();
}

void syncWifiConfigFromSystem(const char *reason = nullptr) {
  String nextSsid = wm.getWiFiSSID(true);
  if (nextSsid.length() == 0 && wifiConnected()) nextSsid = WiFi.SSID();
  if (nextSsid.length() == 0) return;

  String nextPass = wm.getWiFiPass(true);
  bool changed = nextSsid != wifiSsidConfig || nextPass != wifiPassConfig;
  wifiSsidConfig = nextSsid;
  wifiPassConfig = nextPass;

  if (changed) {
    saveWifiConfig();
    if (reason != nullptr && strlen(reason) > 0) {
      logLine("[WiFi] Config sincronizzata: " + String(reason) + " -> " + wifiSsidConfig);
    }
  }
}

bool consumeWebRateLimit(WebRateLimitBucket &bucket, unsigned long windowMs, uint16_t maxHits, unsigned long minGapMs) {
  IPAddress clientIp = server.client().remoteIP();
  unsigned long now = millis();

  // Cerca slot esistente per questo IP; tiene traccia dello slot con lastHitMs pi?? vecchio per l'eviction
  int slotIdx = -1;
  int evictIdx = 0;
  unsigned long oldestMs = bucket.slots[0].lastHitMs;
  for (int i = 0; i < RATE_LIMIT_SLOTS; i++) {
    if (bucket.slots[i].ip == clientIp) { slotIdx = i; break; }
    if (bucket.slots[i].lastHitMs < oldestMs) { oldestMs = bucket.slots[i].lastHitMs; evictIdx = i; }
  }
  if (slotIdx < 0) {
    slotIdx = evictIdx;
    bucket.slots[slotIdx].ip = clientIp;
    bucket.slots[slotIdx].windowStartMs = 0;
    bucket.slots[slotIdx].lastHitMs = 0;
    bucket.slots[slotIdx].hits = 0;
  }

  WebRateLimitEntry &e = bucket.slots[slotIdx];
  if (e.windowStartMs == 0 || (long)(now - e.windowStartMs) >= (long)windowMs) {
    e.windowStartMs = now;
    e.hits = 0;
  }
  if (e.lastHitMs != 0 && (long)(now - e.lastHitMs) < (long)minGapMs) return false;
  if (e.hits >= maxHits) return false;

  e.lastHitMs = now;
  e.hits++;
  return true;
}

bool validateOtaChunkHeader(const uint8_t *data, size_t len) {
  if (otaHeaderValidated) return true;
  if (data == nullptr || len == 0) return false;

  size_t missing = sizeof(otaHeaderProbe) - otaHeaderProbeLen;
  size_t toCopy = len < missing ? len : missing;
  memcpy(otaHeaderProbe + otaHeaderProbeLen, data, toCopy);
  otaHeaderProbeLen += toCopy;

  if (otaHeaderProbeLen < sizeof(otaHeaderProbe)) return true;
  otaHeaderValidated = otaChunkLooksLikeAppImage(otaHeaderProbe, sizeof(otaHeaderProbe));
  return otaHeaderValidated;
}

bool guardJsonRateLimit(WebRateLimitBucket &bucket, unsigned long windowMs, uint16_t maxHits, unsigned long minGapMs, const char *errorCode) {
  if (consumeWebRateLimit(bucket, windowMs, maxHits, minGapMs)) return true;
  sendJson(429, "{\"ok\":false,\"error\":\"" + jsonEscape(String(errorCode)) + "\"}");
  return false;
}

bool guardTextRateLimit(WebRateLimitBucket &bucket, unsigned long windowMs, uint16_t maxHits, unsigned long minGapMs, const char *message) {
  if (consumeWebRateLimit(bucket, windowMs, maxHits, minGapMs)) return true;
  sendText(429, message);
  return false;
}

bool otaFilenameLooksUnsafe(const String &filename) {
  String lower = filename;
  lower.toLowerCase();
  return lower.indexOf("bootloader") >= 0 ||
         lower.indexOf("partition") >= 0 ||
         lower.indexOf("partitions") >= 0;
}

bool otaChunkLooksLikeAppImage(const uint8_t *data, size_t len) {
  if (data == nullptr || len < sizeof(esp_image_header_t)) return false;
  const esp_image_header_t *hdr = reinterpret_cast<const esp_image_header_t *>(data);
  if (hdr->magic != ESP_IMAGE_HEADER_MAGIC) return false;
  if (hdr->segment_count == 0 || hdr->segment_count > ESP_IMAGE_MAX_SEGMENTS) return false;
  if (hdr->chip_id != ESP_CHIP_ID_INVALID && hdr->chip_id != ESP_CHIP_ID_ESP32S2) return false;
  return true;
}

bool verifyFlashedOtaPartition(const esp_partition_t *partition, String &statusOut) {
  if (partition == nullptr) {
    statusOut = "target_partition_missing";
    return false;
  }

  esp_partition_pos_t pos;
  pos.offset = partition->address;
  pos.size = partition->size;
  esp_image_metadata_t metadata;
  esp_err_t verifyErr = esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &metadata);
  if (verifyErr != ESP_OK) {
    statusOut = "partition_verify_failed";
    return false;
  }
  if (metadata.image.chip_id != ESP_CHIP_ID_INVALID && metadata.image.chip_id != ESP_CHIP_ID_ESP32S2) {
    statusOut = "wrong_chip_family";
    return false;
  }

  esp_app_desc_t appDesc;
  esp_err_t appDescErr = esp_ota_get_partition_description(partition, &appDesc);
  if (appDescErr != ESP_OK) {
    statusOut = "missing_app_descriptor";
    return false;
  }
  if (appDesc.project_name[0] == '\0' || appDesc.version[0] == '\0') {
    statusOut = "invalid_app_descriptor";
    return false;
  }

  statusOut = "verified_app_image";
  return true;
}

void restoreRunningPartitionBootTarget() {
  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running != nullptr) {
    esp_ota_set_boot_partition(running);
  }
}

// Low-level Telegram API helpers ??? caller MUST hold telegramHttpMutex.
// Crea un WiFiClientSecure locale per ogni richiesta (pattern ufficiale ESP32 3.x).
static void copyCStringTrunc(char *dest, size_t destSize, const char *src) {
  if (dest == nullptr || destSize == 0) return;
  if (src == nullptr) {
    dest[0] = '\0';
    return;
  }

  size_t i = 0;
  for (; i + 1 < destSize && src[i] != '\0'; ++i) dest[i] = src[i];
  dest[i] = '\0';
}

static void tgBuildPath(char *path, size_t pathSize, const char *method, const char *params = nullptr) {
  if (path == nullptr || pathSize == 0) return;
  path[0] = '\0';
  if (method == nullptr || method[0] == '\0') return;

  const int written = snprintf(
    path,
    pathSize,
    "%s%s%s%s%s",
    "/bot",
    TELEGRAM_BOT_TOKEN,
    "/",
    method,
    (params != nullptr && params[0] != '\0') ? "?" : ""
  );

  if (written < 0 || (size_t)written >= pathSize) {
    path[pathSize - 1] = '\0';
    return;
  }

  if (params != nullptr && params[0] != '\0') {
    const size_t used = (size_t)written;
    snprintf(path + used, pathSize - used, "%s", params);
  }
}

static bool tgSerializeJsonBody(const JsonDocument &doc, char *body, size_t bodySize, size_t &bodyLen) {
  if (body == nullptr || bodySize == 0) {
    setTgLastError("json_body_null");
    return false;
  }
  bodyLen = measureJson(doc);
  if (bodyLen == 0 || bodyLen >= bodySize) {
    setTgLastError("json_body_oversize");
    return false;
  }
  serializeJson(doc, body, bodySize);
  return true;
}

struct PsramJsonAllocator {
  void *allocate(size_t size) {
    if (psramFound()) {
      void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (ptr != nullptr) return ptr;
    }
    return malloc(size);
  }

  void deallocate(void *ptr) {
    if (ptr != nullptr) heap_caps_free(ptr);
  }

  void *reallocate(void *ptr, size_t new_size) {
    if (psramFound()) {
      void *moved = heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (moved != nullptr) return moved;
    }
    return realloc(ptr, new_size);
  }
};

static bool tgParseApiDoc(const JsonDocument &doc, bool &okOut, char *descOut = nullptr, size_t descOutSize = 0, long *messageIdOut = nullptr) {
  okOut = false;
  if (descOut != nullptr && descOutSize > 0) descOut[0] = '\0';
  if (messageIdOut != nullptr) *messageIdOut = 0;

  if (doc["ok"].is<bool>()) okOut = doc["ok"].as<bool>();
  const char *desc = doc["description"] | "";
  if (descOut != nullptr && descOutSize > 0 && desc[0] != '\0') {
    copyCStringTrunc(descOut, descOutSize, desc);
  }
  if (messageIdOut != nullptr) {
    *messageIdOut = doc["result"]["message_id"] | 0L;
  }
  return doc["ok"].is<bool>() || (descOut != nullptr && descOut[0] != '\0');
}

static bool tgDescriptionContains(const String &desc, const char *needle) {
  if (needle == nullptr || needle[0] == '\0') return false;
  String hay = desc;
  hay.toLowerCase();
  String ndl = String(needle);
  ndl.toLowerCase();
  return hay.indexOf(ndl) >= 0;
}

static void tgConfigureClient(WiFiClientSecure &client, int ioTimeoutMs = 20000) {
  if (ioTimeoutMs < 5000) ioTimeoutMs = 5000;
  client.setInsecure();
  client.setTimeout((uint32_t)ioTimeoutMs);            // Stream read timeout
  client.setConnectionTimeout((uint32_t)TELEGRAM_HTTP_CONNECT_TIMEOUT_MS);  // TCP connect timeout
}

static bool tgPostJson(const char *method, const uint8_t *body, size_t bodyLen, int timeoutMs, JsonDocument &doc, bool &bodyPresent, JsonDocument *filterDoc = nullptr) {
  setTgLastError("");
  bodyPresent = false;
  char path[TELEGRAM_API_PATH_CAPACITY];
  tgBuildPath(path, sizeof(path), method);
  WiFiClientSecure client;
  tgConfigureClient(client, timeoutMs + 2000);
  HTTPClient http;
  ScopedTelegramHttpInFlight httpInFlight;
  if (!http.begin(client, "api.telegram.org", 443, path, true)) {
    tgLastHttpCode = -1;
    setTgLastError("http_begin_failed");
    return false;
  }
  http.useHTTP10(true);
  http.setReuse(false);
  http.setTimeout(timeoutMs);
  http.addHeader("Content-Type", "application/json");
  tgLastHttpCode = http.sendRequest("POST", const_cast<uint8_t *>(body), bodyLen);
  bool parsed = false;
  if (tgLastHttpCode > 0) {
    Stream &stream = http.getStream();
    DeserializationError err = filterDoc != nullptr
      ? deserializeJson(doc, stream, DeserializationOption::Filter(*filterDoc))
      : deserializeJson(doc, stream);
    if (err == DeserializationError::Ok) {
      parsed = true;
      bodyPresent = true;
      bool ok = false;
      char desc[TELEGRAM_API_DESC_CAPACITY];
      if (tgParseApiDoc(doc, ok, desc, sizeof(desc)) && !ok && desc[0] != '\0') {
        setTgLastError(desc);
      }
    } else if (err != DeserializationError::EmptyInput) {
      setTgLastError(err.c_str());
    }
  }
  if (tgLastErrorDescriptionBuf[0] == '\0' && tgLastHttpCode != 200) {
    setTgLastError(("http_" + String(tgLastHttpCode)).c_str());
  }
  http.end();
  return parsed;
}

static bool tgGetJson(const char *method, const char *params, int timeoutMs, JsonDocument &doc, bool &bodyPresent, JsonDocument *filterDoc = nullptr) {
  setTgLastError("");
  bodyPresent = false;
  char path[TELEGRAM_API_PATH_CAPACITY];
  tgBuildPath(path, sizeof(path), method, params);
  WiFiClientSecure client;
  tgConfigureClient(client, timeoutMs + 2000);
  HTTPClient http;
  ScopedTelegramHttpInFlight httpInFlight;
  if (!http.begin(client, "api.telegram.org", 443, path, true)) {
    tgLastHttpCode = -1;
    setTgLastError("http_begin_failed");
    return false;
  }
  http.useHTTP10(true);
  http.setReuse(false);
  http.setTimeout(timeoutMs);
  tgLastHttpCode = http.GET();
  bool parsed = false;
  if (tgLastHttpCode > 0) {
    Stream &stream = http.getStream();
    DeserializationError err = filterDoc != nullptr
      ? deserializeJson(doc, stream, DeserializationOption::Filter(*filterDoc))
      : deserializeJson(doc, stream);
    if (err == DeserializationError::Ok) {
      parsed = true;
      bodyPresent = true;
      if (doc["ok"].is<bool>() && !doc["ok"].as<bool>()) {
        const char *desc = doc["description"] | "";
        if (desc[0] != '\0') setTgLastError(desc);
      }
    } else if (err != DeserializationError::EmptyInput) {
      setTgLastError(err.c_str());
    }
  }
  if (tgLastErrorDescriptionBuf[0] == '\0' && tgLastHttpCode != 200) {
    setTgLastError(("http_" + String(tgLastHttpCode)).c_str());
  }
  http.end();
  return parsed;
}

String telegramInlineKeyboardJson() {
  return
    "["
      "["
        "{\"text\":\"\\ud83d\\udce1 IP rete\",\"callback_data\":\"/espip\"},"
        "{\"text\":\"\\u23f3 Uptime\",\"callback_data\":\"/espuptime\"}"
      "],"
      "["
        "{\"text\":\"\\ud83e\\uddfe Stato completo\",\"callback_data\":\"/espstatus\"},"
        "{\"text\":\"\\u23fb Accendi PC\",\"callback_data\":\"/esppulse\"}"
      "],"
      "["
        "{\"text\":\"\\ud83d\\uded1 Spegni forzato\",\"callback_data\":\"/espforce\"},"
        "{\"text\":\"\\u25b6 Test ON\",\"callback_data\":\"/espteston\"}"
      "],"
      "["
        "{\"text\":\"\\u23f9 Test OFF\",\"callback_data\":\"/esptestoff\"},"
        "{\"text\":\"\\ud83d\\udd04 Riavvia ESP\",\"callback_data\":\"/espreboot\"}"
      "]"
    "]";
}

String telegramConfirmKeyboardJson(uint32_t nonce) {
  return
    "["
      "["
        "{\"text\":\"Si\",\"callback_data\":\"/confirm_yes:" + String(nonce) + "\"},"
        "{\"text\":\"No\",\"callback_data\":\"/confirm_no:" + String(nonce) + "\"}"
      "]"
    "]";
}

void scheduleTelegramWebhookRecovery(const String &reason, unsigned long delayMs = 0) {
  if (!telegramConfigured()) return;
  setTelegramWebhookRecoveryState(reason, millis() + delayMs);
}

bool telegramWebhookConflictDetected(const String &desc) {
  return tgDescriptionContains(desc, "webhook") ||
         tgDescriptionContains(desc, "can't use getupdates method while webhook is active");
}

bool tryTelegramDeleteWebhook(bool dropPendingUpdates, const String &reason) {
  if (!telegramConfigured()) return false;
  if (!wifiConnected()) return false;
  if (telegramHttpMutex == nullptr) return false;
  if (xSemaphoreTake(telegramHttpMutex, pdMS_TO_TICKS(1200)) != pdTRUE) {
    logLine("[TG][RECOVERY] deleteWebhook skipped: mutex busy");
    return false;
  }

  ScopedHeapProbe heapProbe("tg_rcv", 1602, heapProbeTelegramRecovery);
  BasicJsonDocument<PsramJsonAllocator> req(TELEGRAM_SMALL_JSON_CAPACITY);
  req["drop_pending_updates"] = dropPendingUpdates;
  char body[TELEGRAM_SMALL_BODY_CAPACITY];
  size_t bodyLen = 0;
  bool serialized = tgSerializeJsonBody(req, body, sizeof(body), bodyLen);
  BasicJsonDocument<PsramJsonAllocator> respDoc(TELEGRAM_SMALL_JSON_CAPACITY);
  bool bodyPresent = false;
  bool parsed = false;
  if (serialized) {
    parsed = tgPostJson("deleteWebhook", reinterpret_cast<uint8_t *>(body), bodyLen, (int)TELEGRAM_WEBHOOK_HTTP_TIMEOUT_MS, respDoc, bodyPresent);
  }
  xSemaphoreGive(telegramHttpMutex);

  bool apiOk = false;
  char desc[TELEGRAM_API_DESC_CAPACITY];
  bool parsedReply = parsed && bodyPresent && tgParseApiDoc(respDoc, apiOk, desc, sizeof(desc));
  bool ok = parsedReply && apiOk;
  if (ok) {
    telegramMetricWebhookRecoveries++;
    telegramLastWebhookRecoveryMs = millis();
    maybeLogTelegramStageSnapshot("tg_rec_ok", 1625, telegramMetricWebhookRecoveries);
    logLine("[TG][RECOVERY] deleteWebhook OK (" + reason + ")");
    telegramRegisterSuccess();
    return true;
  }

  logLine("[TG][RECOVERY] deleteWebhook KO (" + reason + "), HTTP " + String(tgLastHttpCode) +
          (tgLastErrorDescriptionBuf[0] != '\0' ? String(" desc=") + tgLastErrorDescriptionBuf : String("")));
  telegramRegisterError("deleteWebhook_http_" + String(tgLastHttpCode));
  return false;
}

void processTelegramWebhookRecovery() {
  if (!telegramWebhookRecoveryPending) return;
  if (!telegramConfigured()) { telegramWebhookRecoveryPending = false; return; }
  if (!wifiConnected()) return;
  // Keep webhook recovery active even in failsafe, otherwise
  // getUpdates may stay blocked for the entire pause window.
  unsigned long now = millis();
  if (telegramWebhookRecoveryNotBeforeMs != 0 &&
      (long)(now - telegramWebhookRecoveryNotBeforeMs) < 0) return;
  if (telegramLastWebhookRecoveryMs != 0 &&
      (long)(now - telegramLastWebhookRecoveryMs) < (long)TELEGRAM_WEBHOOK_RECOVERY_COOLDOWN_MS) return;

  String reason = telegramWebhookRecoveryReasonSnapshot();
  bool ok = tryTelegramDeleteWebhook(false, reason);
  if (ok) {
    clearTelegramWebhookRecoveryState();
  } else {
    setTelegramWebhookRecoveryState(reason, millis() + TELEGRAM_RETRY_MS);
  }
}

void persistTelegramOffsetIfNeeded(bool force = false) {
  if (telegramLastUpdateId <= 0) return;
  if (!force) {
    if (telegramLastUpdateId == telegramLastOffsetSaved) return;
    if (telegramLastOffsetSaveMs != 0 &&
        (long)(millis() - telegramLastOffsetSaveMs) < (long)TELEGRAM_OFFSET_SAVE_INTERVAL_MS) return;
  }
  saveTelegramState();
  telegramLastOffsetSaved = telegramLastUpdateId;
  telegramLastOffsetSaveMs = millis();
}

void scheduleTelegramNotification(const String &reason, unsigned long delayMs = 0) {
  if (!telegramConfigured()) return;
  if (reason == "boot" &&
      (telegramBootNotifyAttempted || telegramBootNotifyAttempts >= TELEGRAM_BOOT_NOTIFY_RETRY_MAX)) return;
  setTelegramNotifyState(reason, millis() + delayMs);
}

bool sendTelegramMessageEx(const String &text, long replyMessageId = 0, const String &replyMarkup = "") {
  if (!telegramConfigured()) return false;
  if (!wifiConnected()) return false;
  if (telegramInFailsafePause()) return false;

  unsigned long startedMs = millis();

  uint32_t maxBlock = ESP.getMaxAllocHeap();
  logLine("[TG][SEND] heap=" + String(ESP.getFreeHeap()) + " maxBlock=" + String(maxBlock));
  if (maxBlock < 28000) {
    logLine("[TG][SEND] Heap insufficiente (" + String(maxBlock) + " B), invio annullato");
    telegramMetricSendKo++;
    telegramRegisterError("low_heap");
    return false;
  }

  ScopedHeapProbe heapProbe("tg_send", 1601, heapProbeTelegramSend);
  if (TELEGRAM_NET_DIAG_VERBOSE) {
    IPAddress resolvedIP;
    int dnsResult = WiFi.hostByName("api.telegram.org", resolvedIP);
    logLine("[TG][SEND] DNS api.telegram.org -> " + (dnsResult == 1 ? resolvedIP.toString() : String("FAILED (") + dnsResult + ")"));
    WiFiClient tcpTest;
    tcpTest.setTimeout(4000);
    bool tcpOk = tcpTest.connect("api.telegram.org", 443);
    logLine("[TG][SEND] TCP test porta 443: " + String(tcpOk ? "OK" : "KO"));
    tcpTest.stop();
  }

  // Keyboard is attached only when explicitly requested (e.g. /start).
  // This keeps callback replies lightweight and reduces transient send failures.
  String markup = replyMarkup;
  bool ok = false;
  const uint8_t sendAttempts = TELEGRAM_SEND_ATTEMPTS;
  const int sendTimeoutMs = (int)TELEGRAM_SEND_TIMEOUT_MS;
  const unsigned long deadlineMs = startedMs + TELEGRAM_SEND_TOTAL_BUDGET_MS;
  String lastSendError = "";

  for (uint8_t phase = 0; phase < 2 && !ok; phase++) {
    bool includeKeyboard = (phase == 0);
    if (!includeKeyboard && replyMarkup.length() > 0) break;  // no fallback phase if explicit markup was requested

    for (uint8_t attempt = 1; attempt <= sendAttempts; attempt++) {
      if ((long)(millis() - deadlineMs) >= 0) {
        lastSendError = "send_budget_exceeded";
        break;
      }
      if (telegramHttpMutex == nullptr) {
        lastSendError = "mutex_null";
        break;
      }
      if (xSemaphoreTake(telegramHttpMutex, pdMS_TO_TICKS(900)) != pdTRUE) {
        lastSendError = "mutex_busy";
        if (attempt < sendAttempts) vTaskDelay(pdMS_TO_TICKS(TELEGRAM_SEND_RETRY_GAP_MS));
        continue;
      }

      BasicJsonDocument<PsramJsonAllocator> reqDoc(TELEGRAM_SEND_JSON_CAPACITY);
      reqDoc["chat_id"] = TELEGRAM_CHAT_ID;
      reqDoc["text"] = text;
      if (replyMessageId > 0) reqDoc["reply_to_message_id"] = replyMessageId;
      String wrappedMarkup;
      if (includeKeyboard && markup.length() > 0) {
        wrappedMarkup.reserve(markup.length() + 24);
        wrappedMarkup = "{\"inline_keyboard\":";
        wrappedMarkup += markup;
        wrappedMarkup += "}";
        reqDoc["reply_markup"] = serialized(wrappedMarkup);
      }
      char body[TELEGRAM_SEND_BODY_CAPACITY];
      size_t bodyLen = 0;
      bool serialized = tgSerializeJsonBody(reqDoc, body, sizeof(body), bodyLen);
      BasicJsonDocument<PsramJsonAllocator> respFilter(TELEGRAM_SMALL_JSON_CAPACITY);
      respFilter["ok"] = true;
      respFilter["description"] = true;
      respFilter["result"]["message_id"] = true;
      BasicJsonDocument<PsramJsonAllocator> respDoc(TELEGRAM_SMALL_JSON_CAPACITY);
      bool bodyPresent = false;
      bool parsed = false;
      if (serialized) {
        parsed = tgPostJson("sendMessage", reinterpret_cast<uint8_t *>(body), bodyLen, sendTimeoutMs, respDoc, bodyPresent, &respFilter);
      }
      xSemaphoreGive(telegramHttpMutex);

      bool apiOk = false;
      char desc[TELEGRAM_API_DESC_CAPACITY];
      long msgId = 0;
      bool parsedReply = parsed && bodyPresent && tgParseApiDoc(respDoc, apiOk, desc, sizeof(desc), &msgId);
      if (parsedReply && apiOk) {
        if (msgId > 0) telegramConsoleMessageId = msgId;
        ok = true;
        break;
      }

      if (!serialized) lastSendError = "json_body_oversize";
      else if (!bodyPresent) lastSendError = "empty_body";
      else if (desc[0] != '\0') lastSendError = desc;
      else if (tgLastErrorDescriptionBuf[0] != '\0') lastSendError = String(tgLastErrorDescriptionBuf);
      else if (!parsedReply) lastSendError = "api_parse";
      else lastSendError = "api_ko";
      if (attempt < sendAttempts) {
        logLine("[TG][SEND] tentativo " + String(attempt) + "/" + String(sendAttempts) +
                " fallito (HTTP " + String(tgLastHttpCode) + ")");
        vTaskDelay(pdMS_TO_TICKS(TELEGRAM_SEND_RETRY_GAP_MS));
      }
    }
  }

  unsigned long elapsedMs = millis() - startedMs;
  logLine("[TG][SEND] sendMessage " + String(ok ? "OK" : "KO") + " in " + String(elapsedMs) + " ms");
  if (!ok) {
    telegramMetricSendKo++;
    logLine("[TG][SEND] HTTP code: " + String(tgLastHttpCode) +
            (lastSendError.length() > 0 ? " desc=" + lastSendError : ""));
    telegramRegisterError("send_http_" + String(tgLastHttpCode));
  } else {
    telegramMetricSendOk++;
    telegramLastSendOkMs = millis();
    maybeLogTelegramStageSnapshot("tg_send_ok", 1626, telegramMetricSendOk);
    telegramRegisterSuccess();
  }
  return ok;
}

bool sendTelegramMessage(const String &text, long replyMessageId = 0) {
  return sendTelegramMessageEx(text, replyMessageId, "");
}

bool deleteTelegramMessage(long messageId) {
  if (messageId <= 0) return false;
  if (!telegramConfigured()) return false;
  if (!wifiConnected()) return false;
  if (telegramHttpMutex == nullptr) return false;
  if (xSemaphoreTake(telegramHttpMutex, pdMS_TO_TICKS(2500)) != pdTRUE) return false;
  BasicJsonDocument<PsramJsonAllocator> doc(TELEGRAM_SMALL_JSON_CAPACITY);
  doc["chat_id"] = TELEGRAM_CHAT_ID;
  doc["message_id"] = messageId;
  char body[TELEGRAM_SMALL_BODY_CAPACITY];
  size_t bodyLen = 0;
  bool serialized = tgSerializeJsonBody(doc, body, sizeof(body), bodyLen);
  BasicJsonDocument<PsramJsonAllocator> respDoc(TELEGRAM_SMALL_JSON_CAPACITY);
  bool bodyPresent = false;
  bool parsed = false;
  if (serialized) {
    parsed = tgPostJson("deleteMessage", reinterpret_cast<uint8_t *>(body), bodyLen, (int)TELEGRAM_SHORT_HTTP_TIMEOUT_MS, respDoc, bodyPresent);
  }
  bool apiOk = false;
  bool ok = parsed && bodyPresent && tgParseApiDoc(respDoc, apiOk) && apiOk;
  xSemaphoreGive(telegramHttpMutex);
  if (!ok) {
    logLine("[TG][DELETE] KO message_id=" + String(messageId) + " HTTP " + String(tgLastHttpCode) +
            (tgLastErrorDescriptionBuf[0] != '\0' ? String(" desc=") + tgLastErrorDescriptionBuf : String("")));
  }
  return ok;
}

bool answerTelegramCallback(const String &callbackId, const String &text = "") {
  if (!telegramConfigured()) return false;
  if (!wifiConnected()) return false;
  if (telegramHttpMutex == nullptr) return false;
  if (xSemaphoreTake(telegramHttpMutex, pdMS_TO_TICKS(2500)) != pdTRUE) return false;
  BasicJsonDocument<PsramJsonAllocator> doc(TELEGRAM_SMALL_JSON_CAPACITY);
  doc["callback_query_id"] = callbackId;
  if (text.length() > 0) doc["text"] = text;
  char body[TELEGRAM_SMALL_BODY_CAPACITY];
  size_t bodyLen = 0;
  bool serialized = tgSerializeJsonBody(doc, body, sizeof(body), bodyLen);
  BasicJsonDocument<PsramJsonAllocator> respDoc(TELEGRAM_SMALL_JSON_CAPACITY);
  bool bodyPresent = false;
  bool parsed = false;
  if (serialized) {
    parsed = tgPostJson("answerCallbackQuery", reinterpret_cast<uint8_t *>(body), bodyLen, (int)TELEGRAM_SHORT_HTTP_TIMEOUT_MS, respDoc, bodyPresent);
  }
  bool apiOk = false;
  bool ok = parsed && bodyPresent && tgParseApiDoc(respDoc, apiOk) && apiOk;
  xSemaphoreGive(telegramHttpMutex);
  if (!ok) {
    logLine("[TG][CALLBACK] KO HTTP " + String(tgLastHttpCode) +
            (tgLastErrorDescriptionBuf[0] != '\0' ? String(" desc=") + tgLastErrorDescriptionBuf : String("")));
  }
  return ok;
}

bool isAuthorizedTelegramChat(const String &chatId) {
  return chatId == String(TELEGRAM_CHAT_ID);
}

bool telegramAllowIncomingCommand(String &reasonOut) {
  unsigned long now = millis();
  reasonOut = "";

  if (telegramCmdThrottleUntilMs != 0 && (long)(now - telegramCmdThrottleUntilMs) < 0) {
    reasonOut = "throttled";
    return false;
  }

  if (telegramCmdWindowStartMs == 0 || (long)(now - telegramCmdWindowStartMs) >= (long)TELEGRAM_CMD_WINDOW_MS) {
    telegramCmdWindowStartMs = now;
    telegramCmdWindowHits = 0;
  }

  if (telegramCmdLastMs != 0 && (long)(now - telegramCmdLastMs) < (long)TELEGRAM_CMD_MIN_GAP_MS) {
    reasonOut = "min_gap";
    telegramMetricCmdRateDrops++;
    return false;
  }

  if (telegramCmdWindowHits >= TELEGRAM_CMD_MAX_HITS) {
    telegramCmdThrottleUntilMs = now + TELEGRAM_CMD_THROTTLE_MS;
    reasonOut = "window_limit";
    telegramMetricCmdRateDrops++;
    return false;
  }

  telegramCmdLastMs = now;
  telegramCmdWindowHits++;
  return true;
}

void telegramLogRateLimit(const String &msg) {
  unsigned long now = millis();
  if (telegramRateLimitLastLogMs == 0 || (long)(now - telegramRateLimitLastLogMs) >= 2000) {
    telegramRateLimitLastLogMs = now;
    logLine(msg);
  }
}

bool telegramInteractiveMode() { return false; }
void telegramBumpInteractiveWindow() {}
void telegramBumpWebActivity() {}
bool webInteractiveMode() { return false; }

void startFallbackAp() {
  if (fallbackApActive) return;

  WiFi.mode(WIFI_AP_STA);
  bool ok = WiFi.softAP(FALLBACK_AP_SSID);
  if (ok) {
    fallbackApActive = true;
    logLine("[WiFi] AP fallback attivo: SSID " + String(FALLBACK_AP_SSID));
    logLine("[WiFi] AP fallback IP: " + WiFi.softAPIP().toString());
  } else {
    logLine("[WiFi] AP fallback KO");
  }
}

void stopFallbackAp() {
  if (!fallbackApActive) return;
  WiFi.softAPdisconnect(true);
  fallbackApActive = false;
  logLine("[WiFi] AP fallback disattivato");
}

void markLoopHeartbeat() {
  loopHeartbeatMs = millis();
}

void markTelegramHeartbeat() {
  telegramHeartbeatMs = millis();
}

bool telegramInFailsafePause() {
  if (telegramFailsafeUntilMs == 0) return false;
  return (long)(millis() - telegramFailsafeUntilMs) < 0;
}

void requestTelegramHeapRecovery(
  uint32_t freeHeap,
  uint32_t maxBlock,
  const char *reason = "heap_guard",
  uint16_t logCode = 1313
) {
  portENTER_CRITICAL(&telegramStateMux);
  telegramHeapRecoveryRequested = true;
  telegramHeapRecoveryFreeHeap = freeHeap;
  telegramHeapRecoveryMaxBlock = maxBlock;
  telegramHeapRecoveryReasonCode = logCode;
  if (reason == nullptr || reason[0] == '\0') {
    telegramHeapRecoveryReasonBuf[0] = '\0';
  } else {
    size_t i = 0;
    for (; i + 1 < sizeof(telegramHeapRecoveryReasonBuf) && reason[i] != '\0'; ++i) {
      telegramHeapRecoveryReasonBuf[i] = reason[i];
    }
    telegramHeapRecoveryReasonBuf[i] = '\0';
  }
  portEXIT_CRITICAL(&telegramStateMux);

  unsigned long now = millis();
  if (telegramHeapRecoveryLastLogMs != 0 &&
      (long)(now - telegramHeapRecoveryLastLogMs) < 30000L) {
    return;
  }

  char msg[64];
  snprintf(
    msg,
    sizeof(msg),
    "%s fh=%lu mb=%lu",
    (reason != nullptr && reason[0] != '\0') ? reason : "heap_guard",
    (unsigned long)freeHeap,
    (unsigned long)maxBlock
  );
  log_event(BUGLOG_WARN, logCode, msg);
  logLine("[TG] Recovery richiesto (" +
          String((reason != nullptr && reason[0] != '\0') ? reason : "heap_guard") +
          "): fh=" + String(freeHeap) + " mb=" + String(maxBlock));
  telegramHeapRecoveryLastLogMs = now;
}

bool telegramInBackoff() {
  if (telegramBackoffUntilMs == 0) return false;
  return (long)(millis() - telegramBackoffUntilMs) < 0;
}

void telegramRegisterSuccess() {
  telegramConsecutiveErrors = 0;
  telegramBackoffUntilMs = 0;
  telegramFailsafeUntilMs = 0;
}

void telegramRegisterError(const String &reason) {
  if (telegramInFailsafePause()) return;

  if (telegramConsecutiveErrors < 255) telegramConsecutiveErrors++;
  unsigned long backoff = TELEGRAM_ERROR_BACKOFF_BASE_MS * telegramConsecutiveErrors;
  if (backoff > TELEGRAM_ERROR_BACKOFF_MAX_MS) backoff = TELEGRAM_ERROR_BACKOFF_MAX_MS;
  telegramBackoffUntilMs = millis() + backoff;

  if (telegramConsecutiveErrors >= TELEGRAM_MAX_CONSECUTIVE_ERRORS) {
    telegramFailsafeUntilMs = millis() + TELEGRAM_FAILSAFE_PAUSE_MS;
    telegramConsecutiveErrors = 0;
    log_event(BUGLOG_WARN, 1312, reason);
    logLine("[TG] Failsafe: pausa " + String(TELEGRAM_FAILSAFE_PAUSE_MS / 1000UL) + " s");
    return;
  }

  if (reason.length() > 0) {
    logLine("[TG] Errore " + reason + ", backoff " + String(backoff) + " ms");
  }
}

void clearTelegramRuntimeState(bool resetPersistedOffset = false) {
  telegramConsecutiveErrors = 0;
  telegramBackoffUntilMs = 0;
  telegramFailsafeUntilMs = 0;
  telegramHeapRecoveryRequested = false;
  telegramHeapRecoveryFreeHeap = 0;
  telegramHeapRecoveryMaxBlock = 0;
  telegramHeapRecoveryLastLogMs = 0;
  telegramPollOkSinceRestart = 0;
  telegramHeapRecoveryReasonCode = 1313;
  telegramHeapRecoveryReasonBuf[0] = '\0';
  telegramNotifyPending = false;
  telegramNextTryMs = 0;
  telegramWebhookRecoveryPending = false;
  telegramWebhookRecoveryReason[0] = '\0';
  telegramWebhookRecoveryNotBeforeMs = 0;
  telegramLastPollOkMs = 0;
  telegramLastReceiveMs = 0;
  telegramLastSendOkMs = 0;
  setTgLastError("");
  markTelegramHeartbeat();

  if (!resetPersistedOffset) return;

  telegramLastUpdateId = 0;
  telegramLastOffsetSaved = 0;
  telegramLastOffsetSaveMs = 0;
  if (!lockPrefs()) return;
  prefs.begin("pcpower", false);
  prefs.remove("tg_upd_id");
  prefs.end();
  unlockPrefs();
}

bool startTelegramTask(const char *reason = nullptr) {
  if (safeModeActive || !TELEGRAM_TASK_AUTOSTART) return false;
  if (telegramTaskHandle != nullptr) return true;
  if (telegramHttpMutex == nullptr) {
    telegramHttpMutex = xSemaphoreCreateMutex();
    if (telegramHttpMutex == nullptr) {
      logLine("[SYS] telegramHttpMutex create FAILED");
      log_event(BUGLOG_ERROR, 1303, "telegram_http_mutex_create_failed");
      return false;
    }
  }

  BaseType_t tgCreateOk = xTaskCreate(
    telegramTask,
    "telegramTask",
    20480,
    nullptr,
    1,
    &telegramTaskHandle
  );
  if (tgCreateOk != pdPASS || telegramTaskHandle == nullptr) {
    logLine("[SYS] telegramTask create FAILED");
    log_event(BUGLOG_ERROR, 1301, "telegram_task_create_failed");
    appendPersistentBootEvent("task_create_failed name=telegramTask");
    safeModeActive = true;
    return false;
  }

  telegramTaskLastRestartMs = millis();
  telegramPollOkSinceRestart = 0;
  if (reason != nullptr && reason[0] != '\0') {
    logLine("[TG] Task avviato (" + String(reason) + ")");
  } else {
    logLine("[TG] Task avviato");
  }
  return true;
}

bool restartTelegramTask(const String &reason, bool resetPersistedOffset = false) {
  String reasonText = reason.length() > 0 ? reason : String("manual");

  if (telegramTaskHandle != nullptr) {
    TaskHandle_t staleHandle = telegramTaskHandle;
    telegramTaskHandle = nullptr;
    vTaskDelete(staleHandle);
  }

  if (telegramHttpMutex != nullptr) {
    vSemaphoreDelete(telegramHttpMutex);
    telegramHttpMutex = nullptr;
  }

  clearTelegramRuntimeState(resetPersistedOffset);
  telegramTaskRestartCount++;
  log_event(BUGLOG_WARN, resetPersistedOffset ? 1311 : 1310, reasonText);
  logLine("[TG] Recovery task (" + reasonText + (resetPersistedOffset ? ", reset offset" : "") + ")");

  bool started = startTelegramTask(reasonText.c_str());
  if (started && telegramConfigured() &&
      (resetPersistedOffset || TELEGRAM_AUTO_RECOVERY_FOLLOWUP_ENABLED)) {
    scheduleTelegramWebhookRecovery(resetPersistedOffset ? "manual_reset_offset" : "task_recover", 200);
    scheduleTelegramNotification(resetPersistedOffset ? "tg_recover_offset" : "tg_recover", 1200);
  }
  return started;
}

void maybeRecoverTelegramTask() {
  if (!TELEGRAM_TASK_AUTOSTART || safeModeActive || otaUploadInProgress) return;
  if (!telegramConfigured() || !wifiConnected()) return;

  unsigned long now = millis();
  if (telegramTaskHandle == nullptr) {
    startTelegramTask("auto_missing");
    return;
  }

  if (telegramHeapRecoveryRequested) {
    if (telegramTaskLastRestartMs == 0 ||
        (long)(now - telegramTaskLastRestartMs) >= (long)TELEGRAM_HEAP_RECOVERY_RESTART_COOLDOWN_MS) {
      String recoveryReason = telegramHeapRecoveryReasonSnapshot();
      char reason[64];
      snprintf(
        reason,
        sizeof(reason),
        "%s fh=%lu mb=%lu",
        recoveryReason.length() > 0 ? recoveryReason.c_str() : "heap_guard",
        (unsigned long)telegramHeapRecoveryFreeHeap,
        (unsigned long)telegramHeapRecoveryMaxBlock
      );
      if (restartTelegramTask(String(reason))) {
        telegramHeapRecoveryRequested = false;
      }
    }
    return;
  }

  if (telegramHeartbeatMs == 0) return;
  unsigned long heartbeatAgeMs = now - telegramHeartbeatMs;
  if (heartbeatAgeMs < TELEGRAM_TASK_STALE_RESTART_MS) return;
  if (telegramTaskLastRestartMs != 0 &&
      (long)(now - telegramTaskLastRestartMs) < (long)TELEGRAM_TASK_RESTART_COOLDOWN_MS) return;

  restartTelegramTask("stale_heartbeat");
}

void maybeRequestTelegramSoftRecycle() {
  uint32_t freeHeapAfterPoll = ESP.getFreeHeap();
  uint32_t maxBlockAfterPoll = ESP.getMaxAllocHeap();
  bool recycleForHeap = maxBlockAfterPoll < TELEGRAM_SOFT_RECYCLE_MAX_BLOCK_BYTES;
  bool recycleForPollBudget = telegramPollOkSinceRestart >= TELEGRAM_SOFT_RECYCLE_POLL_OK_LIMIT;
  bool recycleForTaskAge =
    telegramTaskLastRestartMs != 0 &&
    (long)(millis() - telegramTaskLastRestartMs) >= (long)TELEGRAM_SOFT_RECYCLE_TASK_AGE_MS;
  if (!recycleForHeap && !recycleForPollBudget && !recycleForTaskAge) return;

  const char *recycleReason =
    recycleForHeap ? "soft_recycle" :
    (recycleForPollBudget ? "poll_budget" : "task_age");
  requestTelegramHeapRecovery(freeHeapAfterPoll, maxBlockAfterPoll, recycleReason, 1314);
  telegramFailsafeUntilMs = millis() + TELEGRAM_SOFT_RECYCLE_PAUSE_MS;
}

String deviceStatusMessage() {
  ScopedHeapProbe heapProbe("tg_stat", 1606, heapProbeTelegramStatusMsg);
  ensureTimeSynced(TIME_SYNC_TIMEOUT_MS);

  int rssi = WiFi.RSSI();
  int pct = rssiToPercent(rssi);
  String msg;
  msg.reserve(384);
  msg = chipModelString() + "\n";
  msg += "ROMA " + formatLocalTimeRome() + "\n";
  msg += "IP " + WiFi.localIP().toString() + "\n";
  msg += "FW " + firmwareVersionString() + "\n";
  msg += "BOOT " + bootSessionIdHex() + "\n";
  msg += "SLOT " + runningPartitionLabel() + "\n";
  msg += "MAC: " + WiFi.macAddress() + "\n";
  msg += "HOST " + String(WiFi.getHostname()) + "\n";
  msg += "SSID " + WiFi.SSID() + "\n";
  msg += "RSSI " + String(rssi) + "dBm " + String(pct) + "%\n";
  msg += "UP " + uptimeHuman() + "\n";
  msg += "MODE " + modeString() + "\n";
  msg += "P " + String(pulseMs) + "ms F " + String(forceMs) + "ms\n";
  msg += "HEAP " + String(ESP.getFreeHeap()) + "B";
  return msg;
}

String telegramIpMessage(const String &reason = "") {
  String msg;
  msg.reserve(256);
  msg = "ESP32-S2 online";
  if (reason.length() > 0) msg += "\nEVENT " + reason;
  msg += "\nIP " + WiFi.localIP().toString();
  msg += "\nHOST " + String(WiFi.getHostname());
  msg += "\nSSID " + WiFi.SSID();
  msg += "\nSLOT " + runningPartitionLabel();
  msg += "\nFW " + String(FW_VERSION);
  msg += "\nBOOT " + bootSessionIdHex();
  return msg;
}

String telegramHelpMessage() {
  return
    "Comandi disponibili:\n"
    "/start\n"
    "/ip\n"
    "/status\n"
    "/help\n\n"
    "Telegram e read-only: nessun comando remoto di reboot, GPIO o OTA.";
}

void notifyTelegramOnline() {
  String notifyReason = telegramNotifyReasonSnapshot();
  bool isBootNotify = (notifyReason == "boot");
  if (isBootNotify && telegramBootNotifyAttempted) {
    clearTelegramNotifyPending();
    return;
  }
  if (isBootNotify && telegramBootNotifyAttempts < 255) telegramBootNotifyAttempts++;

  String msg = telegramIpMessage(notifyReason);
  msg += "\nRST " + String(resetReasonName(bootResetReason));
  bool ok = sendTelegramMessage(msg);
  if (ok) {
    clearTelegramNotifyPending();
    if (isBootNotify) telegramBootNotifyAttempted = true;
    logLine("[TG] Notifica inviata");
  } else {
    if (isBootNotify) {
      if (telegramBootNotifyAttempts < TELEGRAM_BOOT_NOTIFY_RETRY_MAX) {
        unsigned long retryDelayMs = TELEGRAM_RETRY_MS * telegramBootNotifyAttempts;
        scheduleTelegramNotification("boot", retryDelayMs);
        logLine("[TG] Notifica boot non inviata, retry " +
                String(telegramBootNotifyAttempts) + "/" + String(TELEGRAM_BOOT_NOTIFY_RETRY_MAX));
      } else {
        clearTelegramNotifyPending();
        telegramBootNotifyAttempted = true;
        logLine("[TG] Notifica boot non inviata dopo " + String(TELEGRAM_BOOT_NOTIFY_RETRY_MAX) + " tentativi");
      }
    } else {
      scheduleTelegramNotification(notifyReason, TELEGRAM_RETRY_MS);
      logLine("[TG] Notifica non inviata, ritento");
    }
  }
}

void processTelegramNotify() {
  if (!telegramNotifyPending) return;
  if (telegramInFailsafePause() || telegramInBackoff()) return;
  if (WiFi.status() != WL_CONNECTED) return;
  unsigned long now = millis();
  if ((long)(now - telegramNextTryMs) < 0) return;
  notifyTelegramOnline();
}

void clearTelegramPendingConfirm(bool deleteMessage = true) {
  if (deleteMessage && telegramPendingConfirmMessageId > 0) {
    deleteTelegramMessage(telegramPendingConfirmMessageId);
  }
  telegramPendingConfirmMessageId = 0;
  telegramPendingConfirmNonce = 0;
  telegramPendingConfirmAction = "";
  telegramPendingConfirmExpiresMs = 0;
}

bool sendTelegramConfirmPrompt(const String &action, const String &text, long replyMessageId = 0) {
  clearTelegramPendingConfirm(true);
  uint32_t nonce = esp_random() & 0x7FFFFFFF;
  if (nonce == 0) nonce = 1;
  String markup = telegramConfirmKeyboardJson(nonce);
  bool ok = sendTelegramMessageEx(text, replyMessageId, markup);
  if (ok) {
    telegramPendingConfirmNonce = nonce;
    telegramPendingConfirmAction = action;
    telegramPendingConfirmMessageId = telegramConsoleMessageId;
    telegramPendingConfirmExpiresMs = millis() + TELEGRAM_CONFIRM_TIMEOUT_MS;
  }
  return ok;
}

// Forward declaration (defined later in file; needed because Arduino does not
// auto-declare static/default-parameter functions reliably)
void postDeviceCmd(DeviceCmdType type, unsigned long durationMs = 0, const char *source = "");

String normalizeTelegramCommandToken(const String &text) {
  String cmd = text;
  cmd.trim();
  cmd.toLowerCase();
  int spacePos = cmd.indexOf(' ');
  if (spacePos > 0) cmd = cmd.substring(0, spacePos);
  int atPos = cmd.indexOf('@');
  if (atPos > 0) cmd = cmd.substring(0, atPos);
  return cmd;
}

bool isDirectTelegramTextCommandAllowed(const String &text) {
  String cmd = normalizeTelegramCommandToken(text);
  return cmd == "/start" ||
         cmd == "/ip" ||
         cmd == "/status" ||
         cmd == "/help" ||
         cmd == "/espip" ||
         cmd == "/espstatus" ||
         cmd == "/espuptime" ||
         cmd == "ip" ||
         cmd == "status" ||
         cmd == "help";
}

void handleTelegramCommand(const String &text, long replyMessageId = 0, bool allowConfirm = true) {
  (void)allowConfirm;
  String cmd = normalizeTelegramCommandToken(text);

  if (cmd == "status") cmd = "/status";
  else if (cmd == "ip") cmd = "/ip";
  else if (cmd == "help" || cmd == "?") cmd = "/help";
  else if (cmd == "/espip") cmd = "/ip";
  else if (cmd == "/espstatus" || cmd == "/espuptime") cmd = "/status";

  if (cmd == "/start") {
    String msg = telegramIpMessage("start");
    msg += "\n\n";
    msg += telegramHelpMessage();
    sendTelegramMessage(msg, replyMessageId);
    return;
  }

  if (cmd == "/help") {
    sendTelegramMessage(telegramHelpMessage(), replyMessageId);
    return;
  }

  if (cmd == "/ip") {
    sendTelegramMessage(telegramIpMessage("query"), replyMessageId);
    return;
  }

  if (cmd == "/status") {
    sendTelegramMessage(deviceStatusMessage(), replyMessageId);
    return;
  }

  sendTelegramMessage("Comando non disponibile. Usa /start o /help.", replyMessageId);
}

bool processTelegramCommands() {
  if (!telegramConfigured()) return false;
  if (!wifiConnected()) return false;
  if (otaUploadInProgress) return false;

  int pollTimeoutSec = TELEGRAM_LONG_POLL_SEC;
  if (TELEGRAM_POLL_VERBOSE_LOG) {
    logLine("[TG][POLL] getUpdates start, timeout=" + String(pollTimeoutSec) + " s");
  }
  unsigned long startedMs = millis();
  if (telegramHttpMutex == nullptr) return false;
  if (xSemaphoreTake(telegramHttpMutex, pdMS_TO_TICKS(2500)) != pdTRUE) {
    logLine("[TG][POLL] getUpdates saltato: mutex busy");
    telegramRegisterError("mutex_busy");
    return false;
  }

  ScopedHeapProbe heapProbe("tg_poll", 1600, heapProbeTelegramPoll);
  int readTimeoutMs = (int)(pollTimeoutSec * 1000UL + TELEGRAM_LONG_POLL_GRACE_MS);

  char params[96];
  snprintf(
    params,
    sizeof(params),
    "offset=%ld&timeout=%d&limit=%u&allowed_updates=%%5B%%22message%%22%%5D",
    telegramLastUpdateId + 1,
    pollTimeoutSec,
    (unsigned)TELEGRAM_POLL_RESULT_LIMIT
  );

  BasicJsonDocument<PsramJsonAllocator> filter(TELEGRAM_POLL_FILTER_JSON_CAPACITY);
  filter["ok"] = true;
  JsonArray filterResults = filter["result"].to<JsonArray>();
  JsonObject filterUpdate = filterResults.add<JsonObject>();
  filterUpdate["update_id"] = true;
  JsonObject filterMessage = filterUpdate["message"].to<JsonObject>();
  filterMessage["message_id"] = true;
  filterMessage["text"] = true;
  filterMessage["chat"]["id"] = true;

  BasicJsonDocument<PsramJsonAllocator> doc(TELEGRAM_POLL_JSON_CAPACITY);
  bool bodyPresent = false;
  bool parsed = tgGetJson("getUpdates", params, readTimeoutMs, doc, bodyPresent, &filter);
  xSemaphoreGive(telegramHttpMutex);
  markTelegramHeartbeat();

  unsigned long elapsedMs = millis() - startedMs;
  bool httpOk = (tgLastHttpCode == 200);

  if (!httpOk) {
    telegramMetricPollKo++;

    // Timeout di long-poll: transitorio, non va trattato come hard error.
    if (tgLastHttpCode == -11) {
      if (TELEGRAM_POLL_VERBOSE_LOG) {
        logLine("[TG][POLL] timeout I/O ignorato in " + String(elapsedMs) + " ms");
      }
      return false;
    }

    // HTTP 409: Telegram segnala conflitto (webhook attivo o doppio consumer).
    if (tgLastHttpCode == 409) {
      logLine("[TG][POLL] conflict 409: " +
              (tgLastErrorDescriptionBuf[0] != '\0' ? String(tgLastErrorDescriptionBuf) : String("no_description")));
      if (telegramWebhookConflictDetected(String(tgLastErrorDescriptionBuf))) {
        scheduleTelegramWebhookRecovery("conflict_409", 300);
        return false;
      }
      telegramRegisterError("getUpdates_conflict_409");
      vTaskDelay(pdMS_TO_TICKS(200));
      return false;
    }

    logLine("[TG][POLL] errore HTTP " + String(tgLastHttpCode) +
            " in " + String(elapsedMs) + " ms" +
            (tgLastErrorDescriptionBuf[0] != '\0' ? String(" desc=") + tgLastErrorDescriptionBuf : String("")));
    telegramRegisterError("getUpdates_http_" + String(tgLastHttpCode));
    vTaskDelay(pdMS_TO_TICKS(200));
    return false;
  }

  if (!bodyPresent) {
    telegramMetricPollKo++;
    logLine("[TG][POLL] risposta 200 ma body vuoto");
    telegramRegisterError("getUpdates_empty_200");
    vTaskDelay(pdMS_TO_TICKS(120));
    return false;
  }

  if (!parsed || !doc["ok"].as<bool>()) {
    telegramMetricPollParseKo++;
    if (TELEGRAM_POLL_VERBOSE_LOG) logLine("[TG][POLL] parse error in " + String(elapsedMs) + " ms");
    telegramRegisterError("getUpdates_parse");
    vTaskDelay(pdMS_TO_TICKS(200));
    return false;
  }

  telegramRegisterSuccess();
  telegramMetricPollOk++;
  if (telegramPollOkSinceRestart < 255) telegramPollOkSinceRestart++;
  telegramLastPollOkMs = millis();
  maybeLogTelegramStageSnapshot("tg_poll_ok", 1627, telegramMetricPollOk);

  JsonArray results = doc["result"].as<JsonArray>();
  int numNewMessages = (int)results.size();

  if (numNewMessages > 0 || TELEGRAM_POLL_VERBOSE_LOG)
    logLine("[TG][POLL] getUpdates messages=" + String(numNewMessages) + " in " + String(elapsedMs) + " ms");

  if (numNewMessages == 0) {
    maybeRequestTelegramSoftRecycle();
    return false;
  }

  bool handled = false;
  for (JsonObject update : results) {
    long updateId = update["update_id"].as<long>();
    if (updateId <= telegramLastUpdateId) continue;

      telegramMetricUpdatesRx++;
      telegramLastReceiveMs = millis();
      telegramLastUpdateId = updateId;
      persistTelegramOffsetIfNeeded(false);

    if (update.containsKey("message")) {
      telegramMetricCommandsRx++;
      JsonObject message = update["message"];
      String chatId = String(message["chat"]["id"].as<long>());
      String text = message["text"] | String("");
      long messageId = message["message_id"].as<long>();

      if (!isAuthorizedTelegramChat(chatId)) {
        logLine("[TG][SEC] Chat non autorizzata ignorata: " + chatId);
        handled = true;
        continue;
      }

      if (text.length() == 0) { handled = true; continue; }

      if (!isDirectTelegramTextCommandAllowed(text)) {
        sendTelegramMessage(
          "Comando non disponibile.\n"
          "Usa solo /start, /ip, /status o /help.",
          messageId
        );
        logLine("[TG][CMD] Testo ignorato (read-only): " + text);
        handled = true;
        continue;
      }

      String cmdGateReason;
      if (!telegramAllowIncomingCommand(cmdGateReason)) {
        sendTelegramMessage("Troppi comandi ravvicinati, attendi qualche secondo.", messageId);
        telegramLogRateLimit("[TG][CMD] Text rate-limited (" + cmdGateReason + ")");
        handled = true;
        continue;
      }

      logLine("[TG][CMD] Cmd: " + text);
      handleTelegramCommand(text, messageId, false);
      handled = true;
    }
  }

  persistTelegramOffsetIfNeeded(false);
  maybeRequestTelegramSoftRecycle();
  return handled;
}

void telegramTask(void *parameter) {
  markTelegramHeartbeat();
  for (;;) {
    markTelegramHeartbeat();

    if (otaUploadInProgress) {
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }

    if (telegramInFailsafePause()) {
      if (wifiConnected() && telegramConfigured() && telegramWebhookRecoveryPending) {
        processTelegramWebhookRecovery();
      }
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    if (wifiConnected() && telegramConfigured()) {
      uint32_t freeHeap = ESP.getFreeHeap();
      uint32_t maxBlock = ESP.getMaxAllocHeap();
      if (freeHeap < TELEGRAM_HEAP_RECOVERY_FREE_HEAP_BYTES ||
          maxBlock < TELEGRAM_HEAP_RECOVERY_MAX_BLOCK_BYTES) {
        requestTelegramHeapRecovery(freeHeap, maxBlock, "heap_guard", 1313);
        telegramFailsafeUntilMs = millis() + TELEGRAM_HEAP_RECOVERY_PAUSE_MS;
        vTaskDelay(pdMS_TO_TICKS(250));
        continue;
      }
      if (maxBlock < TELEGRAM_MIN_MAX_BLOCK_BYTES) {
        logLine("[TG] Heap frammentato, pausa " + String(TELEGRAM_LOW_HEAP_PAUSE_MS / 1000UL) + " s");
        telegramFailsafeUntilMs = millis() + TELEGRAM_LOW_HEAP_PAUSE_MS;
        vTaskDelay(pdMS_TO_TICKS(500));
        continue;
      }
      processTelegramWebhookRecovery();
      if (telegramNotifyPending) {
        processTelegramNotify();
      }
      processTelegramCommands();
      persistTelegramOffsetIfNeeded(false);
    } else {
      vTaskDelay(pdMS_TO_TICKS(TELEGRAM_TASK_RETRY_MS));
    }
    markTelegramHeartbeat();
    vTaskDelay(pdMS_TO_TICKS(25));
  }
}

void safetyTask(void *parameter) {
  for (;;) {
    unsigned long now = millis();

    if (!otaUploadInProgress) {
      if (loopHeartbeatMs != 0 && (long)(now - loopHeartbeatMs) > (long)WATCHDOG_LOOP_STALL_MS) {
        restartFromWatchdog("loop_stall");
      }
    }

    if (otaUploadInProgress &&
        otaUploadWrittenBytes > 0 &&
        otaLastChunkMs > 0 &&
        (long)(now - otaLastChunkMs) > (long)OTA_STALL_REBOOT_MS) {
      otaStallRecoveryRequested = true;
    }

    vTaskDelay(pdMS_TO_TICKS(WATCHDOG_CHECK_EVERY_MS));
  }
}

static bool lockPrefs() {
  if (prefsMutex == nullptr) return true;
  return xSemaphoreTake(prefsMutex, pdMS_TO_TICKS(500)) == pdTRUE;
}

static void unlockPrefs() {
  if (prefsMutex != nullptr) xSemaphoreGive(prefsMutex);
}

void appendPersistentBootEvent(const String &eventLine) {
  if (!lockPrefs()) return;
  prefs.begin("pcpower", false);
  uint32_t nextIndex = prefs.getUInt("boot_hist_idx", 0);
  uint32_t nextCount = prefs.getUInt("boot_hist_count", 0);
  String slotKey = "boot_hist_" + String(nextIndex % BOOT_HISTORY_CAPACITY);
  prefs.putString(slotKey.c_str(), eventLine);
  prefs.putUInt("boot_hist_idx", nextIndex + 1);
  if (nextCount < BOOT_HISTORY_CAPACITY) nextCount++;
  prefs.putUInt("boot_hist_count", nextCount);
  prefs.end();
  unlockPrefs();
}

String bootHistoryJson(int limit = 12) {
  if (limit <= 0) limit = 1;
  if (limit > BOOT_HISTORY_CAPACITY) limit = BOOT_HISTORY_CAPACITY;
  if (!lockPrefs()) return "{\"ok\":false,\"error\":\"prefs_busy\"}";

  prefs.begin("pcpower", true);
  uint32_t nextIndex = prefs.getUInt("boot_hist_idx", 0);
  uint32_t count = prefs.getUInt("boot_hist_count", 0);
  if (count > BOOT_HISTORY_CAPACITY) count = BOOT_HISTORY_CAPACITY;
  uint32_t shown = count < (uint32_t)limit ? count : (uint32_t)limit;

  String json = "{\"ok\":true,\"count\":" + String(count) + ",\"shown\":" + String(shown) + ",\"events\":[";
  for (uint32_t i = 0; i < shown; i++) {
    uint32_t rev = shown - 1 - i;
    uint32_t absoluteIndex = nextIndex - 1 - rev;
    String slotKey = "boot_hist_" + String(absoluteIndex % BOOT_HISTORY_CAPACITY);
    String value = prefs.getString(slotKey.c_str(), "");
    if (i > 0) json += ",";
    json += "\"" + jsonEscape(value) + "\"";
  }
  json += "]}";
  prefs.end();
  unlockPrefs();
  return json;
}

void saveTimings() {
  if (!lockPrefs()) return;
  prefs.begin("pcpower", false);
  prefs.putULong("pulse_ms", pulseMs);
  prefs.putULong("force_ms", forceMs);
  prefs.end();
  unlockPrefs();
}

void loadTimings() {
  if (!lockPrefs()) return;
  prefs.begin("pcpower", true);
  bool hasPulse = prefs.isKey("pulse_ms");
  bool hasForce = prefs.isKey("force_ms");
  pulseMs = prefs.getULong("pulse_ms", DEFAULT_PULSE_MS);
  forceMs = prefs.getULong("force_ms", DEFAULT_FORCE_MS);
  prefs.end();
  unlockPrefs();

  if (pulseMs < 50) pulseMs = DEFAULT_PULSE_MS;
  if (forceMs < 500) forceMs = DEFAULT_FORCE_MS;

  if (!hasPulse || !hasForce) {
    saveTimings();
  }
}

void saveWifiConfig() {
  if (!lockPrefs()) return;
  prefs.begin("pcpower", false);
  prefs.putString("wifi_ssid", wifiSsidConfig);
  prefs.putString("wifi_pass", wifiPassConfig);
  prefs.end();
  unlockPrefs();
}

void saveTelegramState() {
  if (telegramLastUpdateId <= 0) return;
  if (telegramLastUpdateId == telegramLastOffsetSaved) return;
  if (!lockPrefs()) return;
  prefs.begin("pcpower", false);
  prefs.putLong("tg_upd_id", telegramLastUpdateId);
  prefs.end();
  unlockPrefs();
  telegramLastOffsetSaved = telegramLastUpdateId;
  telegramLastOffsetSaveMs = millis();
}

void loadTelegramState() {
  if (!lockPrefs()) return;
  prefs.begin("pcpower", true);
  telegramLastUpdateId = prefs.getLong("tg_upd_id", 0);
  prefs.end();
  unlockPrefs();
  telegramLastOffsetSaved = telegramLastUpdateId;
  telegramLastOffsetSaveMs = millis();
}

void loadWifiConfig() {
  if (!lockPrefs()) return;
  prefs.begin("pcpower", true);
  wifiSsidConfig = prefs.getString("wifi_ssid", "");
  wifiPassConfig = prefs.getString("wifi_pass", "");
  prefs.end();
  unlockPrefs();
}

bool connectConfiguredWifi(unsigned long timeoutMs) {
  if (wifiSsidConfig.length() == 0) return false;

    logLine("[WiFi] Provo config web SSID: " + wifiSsidConfig);

  WiFi.begin(wifiSsidConfig.c_str(), wifiPassConfig.c_str());
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(250);
  }

  WiFi.disconnect();
  return false;
}

void scheduleNodeRedPresenceReport(const String &reason, unsigned long delayMs = 0) {
  if (!nodeRedReportConfigured()) return;
  setNodeRedReportState(reason, millis() + delayMs);
}

String nodeRedPresenceJson(const String &reason) {
  String json;
  json.reserve(320);
  json += "{";
  json += "\"reason\":\"" + jsonEscape(reason) + "\",";
  json += "\"ip\":\"" + jsonEscape(WiFi.localIP().toString()) + "\",";
  json += "\"host\":\"" + jsonEscape(String(WiFi.getHostname())) + "\",";
  json += "\"fw\":\"" + jsonEscape(String(FW_VERSION)) + "\",";
  json += "\"fw_full\":\"" + jsonEscape(firmwareVersionString()) + "\",";
  json += "\"boot_id\":\"" + jsonEscape(bootSessionIdHex()) + "\",";
  json += "\"running_partition\":\"" + jsonEscape(runningPartitionLabel()) + "\",";
  json += "\"wifi_ssid\":\"" + jsonEscape(WiFi.SSID()) + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"rome_time\":\"" + jsonEscape(formatLocalTimeRome()) + "\"";
  json += "}";
  return json;
}

bool sendNodeRedPresenceReport(const String &reason) {
  if (!nodeRedReportConfigured()) return false;
  if (!wifiConnected()) return false;

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, NODERED_REPORT_URL)) {
    logLine("[NR] Presence begin KO");
    return false;
  }

  http.setConnectTimeout((int)NODERED_REPORT_TIMEOUT_MS);
  http.setTimeout((int)NODERED_REPORT_TIMEOUT_MS);
  http.addHeader("Content-Type", "application/json");
  String body = nodeRedPresenceJson(reason);
  int code = http.POST(body);
  http.end();

  if (code > 0 && code < 300) {
    logLine("[NR] Presence report OK (" + reason + ")");
    appendPersistentBootEvent("presence " + reason + " ip=" + WiFi.localIP().toString());
    return true;
  }

  logLine("[NR] Presence report KO (" + reason + "), HTTP " + String(code));
  return false;
}

void processNodeRedPresenceReport() {
  if (!nodeRedReportPending) return;
  if (!nodeRedReportConfigured()) {
    clearNodeRedReportState();
    return;
  }
  if (!wifiConnected() || otaUploadInProgress) return;

  unsigned long dueMs = 0;
  portENTER_CRITICAL(&telegramStateMux);
  dueMs = nodeRedReportDueMs;
  portEXIT_CRITICAL(&telegramStateMux);
  if (dueMs != 0 && (long)(millis() - dueMs) < 0) return;

  String reason = nodeRedReportReasonSnapshot();
  if (sendNodeRedPresenceReport(reason)) {
    clearNodeRedReportState();
  } else {
    setNodeRedReportState(reason, millis() + NODERED_REPORT_RETRY_MS);
  }
}

// =====================
// OUTPUT (solo pin power)
// =====================
void setPcOutput(bool on) {
  digitalWrite(PC_PWR_PIN, on ? HIGH : LOW);
}

void clearTestModeState() {
  testModeEnabled = false;
  testOutputHigh = false;
  testModeUntilMs = 0;
}

// LED status logic:
// - se PULSE o TEST: LED segue lo stato "output high"
// - se WiFi non connessa: LED lampeggia
// - se WiFi connessa: LED acceso fisso
void updateStatusLed() {
  const bool outputHigh = outputIsHigh();

  // Durante comando: LED acceso
  if (outputHigh) {
    digitalWrite(STATUS_LED_PIN, HIGH);
    return;
  }

  // Se connesso: LED SPENTO (default)
  if (wifiConnected()) {
    digitalWrite(STATUS_LED_PIN, LOW);
    return;
  }

  // Se non connesso: lampeggio
  unsigned long now = millis();
  if ((long)(now - ledNextToggleMs) >= 0) {
    ledNextToggleMs = now + LED_BLINK_MS;
    ledBlinkState = !ledBlinkState;
    digitalWrite(STATUS_LED_PIN, ledBlinkState ? HIGH : LOW);
  }
}

String modeString() {
  if (pulseActive) return "pulse";
  if (testModeEnabled) return testOutputHigh ? "test_on" : "test_off";
  return "idle";
}

bool requestPulse(const String &source) {
  return requestPulseMs(pulseMs, source);
}

bool requestPulseMs(unsigned long durationMs, const String &source) {
  unsigned long now = millis();
  if (pulseActive) return false;

  if (testModeEnabled) {
    // Auto-exit test mode when a real pulse is requested.
    clearTestModeState();
    setPcOutput(false);
  }

  if (now - lastPulseMs < LOCKOUT_MS) return false;

  pulseActive = true;
  pulseEndMs = now + durationMs;
  lastPulseMs = now;
  lastTrigger = source;

  setPcOutput(true);
  return true;
}

void setTestOutput(bool high, const String &source) {
  pulseActive = false;
  pulseEndMs = 0;
  testModeEnabled = true;
  testOutputHigh = high;
  testModeUntilMs = high ? (millis() + testModeTimeoutMs) : 0;
  lastTrigger = source;
  setPcOutput(high);
}

void releasePcOutputForSensitiveAction(const String &source) {
  pulseActive = false;
  pulseEndMs = 0;
  clearTestModeState();
  lastTrigger = source;
  setPcOutput(false);
}

void postDeviceCmd(DeviceCmdType type, unsigned long durationMs, const char *source) {
  if (deviceCmdQueue == nullptr) return;
  DeviceCmd cmd;
  cmd.type = type;
  cmd.durationMs = durationMs;
  strncpy(cmd.source, source, sizeof(cmd.source) - 1);
  cmd.source[sizeof(cmd.source) - 1] = '\0';
  xQueueSend(deviceCmdQueue, &cmd, 0);
}

void processDeviceCmdQueue() {
  if (deviceCmdQueue == nullptr) return;
  DeviceCmd cmd;
  while (xQueueReceive(deviceCmdQueue, &cmd, 0) == pdTRUE) {
    String src = String(cmd.source);
    switch (cmd.type) {
      case DCMD_PULSE:
        requestPulse(src);
        break;
      case DCMD_FORCE:
        requestPulseMs(cmd.durationMs > 0 ? cmd.durationMs : forceMs, src);
        break;
      case DCMD_TEST_ON:
        setTestOutput(true, src);
        break;
      case DCMD_TEST_OFF:
        setTestOutput(false, src);
        break;
      case DCMD_REBOOT:
        releasePcOutputForSensitiveAction(src);
        logLine("[SYS] Reboot richiesto via Telegram");
        delay(500);
        markIntentionalReboot();
        ESP.restart();
        break;
    }
  }
}

void updatePulseState() {
  if (!pulseActive) return;
  unsigned long now = millis();
  if ((long)(now - pulseEndMs) >= 0) {
    pulseActive = false;
    setPcOutput(false);
  }
}

void processTestModeTimeout() {
  if (!testModeEnabled || !testOutputHigh || testModeUntilMs == 0) return;
  unsigned long now = millis();
  if ((long)(now - testModeUntilMs) < 0) return;

  clearTestModeState();
  lastTrigger = "test-timeout";
  setPcOutput(false);
  logLine("[TEST] Timeout raggiunto, output rilasciato");
}

void processTelegramPendingConfirmTimeout() {
  if (telegramPendingConfirmMessageId <= 0 || telegramPendingConfirmExpiresMs == 0) return;
  unsigned long now = millis();
  if ((long)(now - telegramPendingConfirmExpiresMs) < 0) return;

  logLine("[TG] Conferma scaduta");
  clearTelegramPendingConfirm(true);
}

void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
}

void addNoCacheHeaders() {
  server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
}

void sendJson(int code, const String &body) {
  telegramBumpWebActivity();
  addCorsHeaders();
  server.send(code, "application/json", body);
}

void sendText(int code, const String &body) {
  telegramBumpWebActivity();
  addCorsHeaders();
  server.send(code, "text/plain", body);
}

String statusJson() {
  ScopedHeapProbe heapProbe("st_json", 1603, heapProbeStatusJson);
  String json;
  json.reserve(2800);  // BUG-4 FIX: ~2.5 KB con tutti i campi; 1600 causava 1-2 riallocazioni heap
  int rssi = WiFi.RSSI();
  unsigned long testRemainingMs = 0;
  unsigned long telegramPauseRemainingMs = 0;
  unsigned long telegramBackoffRemainingMs = 0;
  unsigned long wifiOfflineMs = 0;
  unsigned long now = millis();
  unsigned long telegramLastPollAgeMs = telegramLastPollOkMs > 0 ? (now - telegramLastPollOkMs) : 0;
  unsigned long telegramLastRxAgeMs = telegramLastReceiveMs > 0 ? (now - telegramLastReceiveMs) : 0;
  unsigned long telegramLastSendAgeMs = telegramLastSendOkMs > 0 ? (now - telegramLastSendOkMs) : 0;
  unsigned long telegramLastRecoveryAgeMs = telegramLastWebhookRecoveryMs > 0 ? (now - telegramLastWebhookRecoveryMs) : 0;
  unsigned long telegramHeartbeatAgeMs = telegramHeartbeatMs > 0 ? (now - telegramHeartbeatMs) : 0;
  if (!wifiConnected() && wifiOfflineSinceMs > 0) {
    wifiOfflineMs = now - wifiOfflineSinceMs;
  }
  if (testModeEnabled && testOutputHigh && testModeUntilMs != 0) {
    testRemainingMs = (long)(testModeUntilMs - now) > 0 ? (testModeUntilMs - now) : 0;
    telegramPauseRemainingMs = (long)(telegramFailsafeUntilMs - now) > 0 ? (telegramFailsafeUntilMs - now) : 0;
    telegramBackoffRemainingMs = (long)(telegramBackoffUntilMs - now) > 0 ? (telegramBackoffUntilMs - now) : 0;
  } else {
    telegramPauseRemainingMs = (long)(telegramFailsafeUntilMs - now) > 0 ? (telegramFailsafeUntilMs - now) : 0;
    telegramBackoffRemainingMs = (long)(telegramBackoffUntilMs - now) > 0 ? (telegramBackoffUntilMs - now) : 0;
  }
  json = "{";
  json += "\"ok\":true,";
  json += "\"rome_time\":\"" + jsonEscape(formatLocalTimeRome()) + "\",";
  json += "\"ip\":\"" + jsonEscape(WiFi.localIP().toString()) + "\",";
  json += "\"fw_version\":\"" + jsonEscape(String(FW_VERSION)) + "\",";
  json += "\"fw_build\":\"" + jsonEscape(String(__DATE__) + " " + String(__TIME__)) + "\",";
  json += "\"fw_full\":\"" + jsonEscape(firmwareVersionString()) + "\",";
  json += "\"boot_id\":\"" + jsonEscape(bootSessionIdHex()) + "\",";
  json += "\"running_partition\":\"" + jsonEscape(runningPartitionLabel()) + "\",";
  json += "\"chip_model\":\"" + jsonEscape(chipModelString()) + "\",";
  json += "\"reset_reason\":\"" + jsonEscape(String(resetReasonName(bootResetReason))) + "\",";
  json += "\"wifi_ssid\":\"" + jsonEscape(WiFi.SSID()) + "\",";
  json += "\"wifi_configured_ssid\":\"" + jsonEscape(wifiSsidConfig) + "\",";
  json += "\"wifi_connected\":" + String(wifiConnected() ? "true" : "false") + ",";
  json += "\"ap_mode\":" + String(((WiFi.getMode() & WIFI_AP) != 0) ? "true" : "false") + ",";
  json += "\"fallback_ap_active\":" + String(fallbackApActive ? "true" : "false") + ",";
  json += "\"safe_mode\":" + String(safeModeActive ? "true" : "false") + ",";
  json += "\"crash_count\":" + String(rtcCrashCount) + ",";
  json += "\"softlock_reboot_count\":" + String(rtcSoftLockRebootCount) + ",";
  json += "\"wifi_offline_s\":" + String(wifiOfflineMs / 1000UL) + ",";
  json += "\"rssi\":" + String(rssi) + ",";
  json += "\"signal_pct\":" + String(rssiToPercent(rssi)) + ",";
  json += "\"uptime_s\":" + String(uptimeSeconds()) + ",";
  json += "\"uptime_human\":\"" + jsonEscape(uptimeHuman()) + "\",";
  json += "\"mode\":\"" + jsonEscape(modeString()) + "\",";
  json += "\"pulse_active\":" + String(pulseActive ? "true" : "false") + ",";
  json += "\"test_mode\":" + String(testModeEnabled ? "true" : "false") + ",";
  json += "\"output_high\":" + String(outputIsHigh() ? "true" : "false") + ",";
  json += "\"pulse_ms\":" + String(pulseMs) + ",";
  json += "\"force_ms\":" + String(forceMs) + ",";
  json += "\"test_timeout_ms\":" + String(testModeTimeoutMs) + ",";
  json += "\"test_timeout_remaining_s\":" + String((testRemainingMs + 999UL) / 1000UL) + ",";
  json += "\"telegram_confirm_pending\":" + String((telegramPendingConfirmMessageId > 0) ? "true" : "false") + ",";
  json += "\"telegram_failsafe\":" + String(telegramInFailsafePause() ? "true" : "false") + ",";
  json += "\"telegram_failsafe_remaining_s\":" + String((telegramPauseRemainingMs + 999UL) / 1000UL) + ",";
  json += "\"telegram_backoff_remaining_ms\":" + String(telegramBackoffRemainingMs) + ",";
  json += "\"telegram_task_running\":" + String(telegramTaskHandle != nullptr ? "true" : "false") + ",";
  json += "\"telegram_task_restart_count\":" + String(telegramTaskRestartCount) + ",";
  json += "\"telegram_heartbeat_age_ms\":" + String(telegramHeartbeatAgeMs) + ",";
  json += "\"telegram_last_http_code\":" + String(tgLastHttpCode) + ",";
  json += "\"telegram_last_error\":\"" + jsonEscape(getTgLastError()) + "\",";
  json += "\"telegram_poll_ok_count\":" + String(telegramMetricPollOk) + ",";
  json += "\"telegram_poll_err_count\":" + String(telegramMetricPollKo) + ",";
  json += "\"telegram_poll_parse_err_count\":" + String(telegramMetricPollParseKo) + ",";
  json += "\"telegram_updates_rx_count\":" + String(telegramMetricUpdatesRx) + ",";
  json += "\"telegram_commands_rx_count\":" + String(telegramMetricCommandsRx) + ",";
  json += "\"telegram_send_ok_count\":" + String(telegramMetricSendOk) + ",";
  json += "\"telegram_send_err_count\":" + String(telegramMetricSendKo) + ",";
  json += "\"telegram_cmd_rate_drop_count\":" + String(telegramMetricCmdRateDrops) + ",";
  json += "\"telegram_webhook_recovery_count\":" + String(telegramMetricWebhookRecoveries) + ",";
  json += "\"telegram_webhook_recovery_pending\":" + String(telegramWebhookRecoveryPending ? "true" : "false") + ",";
  json += "\"telegram_last_poll_ok_age_ms\":" + String(telegramLastPollAgeMs) + ",";
  json += "\"telegram_last_rx_age_ms\":" + String(telegramLastRxAgeMs) + ",";
  json += "\"telegram_last_send_ok_age_ms\":" + String(telegramLastSendAgeMs) + ",";
  json += "\"telegram_last_webhook_recovery_age_ms\":" + String(telegramLastRecoveryAgeMs) + ",";
  json += "\"log_count\":" + String(webLogCount) + ",";
  json += "\"last_trigger\":\"" + jsonEscape(lastTrigger) + "\"";
  json += "}";
  return json;
}

String wifiScanJson() {
  ScopedHeapProbe heapProbe("wf_scan", 1605, heapProbeWifiScan);
  if (!wifiScanPending) {
    WiFi.scanDelete();
    int scanStart = WiFi.scanNetworks(true, true);
    if (scanStart == WIFI_SCAN_FAILED) {
      return "{\"ok\":false,\"error\":\"scan_start_failed\",\"count\":0,\"networks\":[]}";
    }
    wifiScanPending = true;
    wifiScanStartedMs = millis();
    return "{\"ok\":true,\"scanning\":true,\"count\":0,\"networks\":[]}";
  }

  int found = WiFi.scanComplete();
  if (found == WIFI_SCAN_RUNNING) {
    if ((long)(millis() - wifiScanStartedMs) >= (long)WIFI_SCAN_TIMEOUT_MS) {
      WiFi.scanDelete();
      wifiScanPending = false;
      wifiScanStartedMs = 0;
      return "{\"ok\":false,\"error\":\"scan_timeout\",\"count\":0,\"networks\":[]}";
    }
    return "{\"ok\":true,\"scanning\":true,\"count\":0,\"networks\":[]}";
  }

  if (found < 0) {
    WiFi.scanDelete();
    wifiScanPending = false;
    wifiScanStartedMs = 0;
    return "{\"ok\":false,\"error\":\"scan_failed\",\"count\":0,\"networks\":[]}";
  }

  int safeCount = found;
  String json;
  json.reserve(64 + (safeCount * 96));
  json = "{\"ok\":true,\"scanning\":false,\"count\":" + String(safeCount) + ",\"networks\":[";

  for (int i = 0; i < safeCount; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"ssid\":\"" + jsonEscape(WiFi.SSID(i)) + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
    json += "\"signal_pct\":" + String(rssiToPercent(WiFi.RSSI(i))) + ",";
    json += "\"open\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false");
    json += "}";
  }

  json += "]}";
  WiFi.scanDelete();
  wifiScanPending = false;
  wifiScanStartedMs = 0;
  return json;
}

const char INDEX_HTML[] PROGMEM =
R"HTML(
<!doctype html>
<html lang="it">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Power Console</title>
)HTML"
"  <link rel=\"icon\" type=\"image/svg+xml\" href=\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'%3E%3Crect width='64' height='64' rx='16' fill='%230f172a'/%3E%3Cpath d='M32 12v18' stroke='%23f8fafc' stroke-width='6' stroke-linecap='round'/%3E%3Cpath d='M21 20a16 16 0 1 0 22 0' fill='none' stroke='%2338bdf8' stroke-width='6' stroke-linecap='round'/%3E%3C/svg%3E\">\n"
R"HTML(
  <style>
    :root{--bg:#0f172a;--bg2:#1e293b;--card:#111827cc;--text:#e5e7eb;--muted:#94a3b8;--line:#334155;--panel:#0b1220aa;}
    *{box-sizing:border-box}
    body{margin:0;min-height:100vh;font-family:"Segoe UI",Tahoma,sans-serif;color:var(--text);background:radial-gradient(circle at 20% 15%, #164e63 0%, transparent 35%),radial-gradient(circle at 80% 85%, #3f1d2e 0%, transparent 35%),linear-gradient(135deg, var(--bg), var(--bg2));padding:16px;}
    .wrap{width:min(1180px,100%);margin:0 auto;background:var(--card);border:1px solid var(--line);border-radius:22px;backdrop-filter:blur(6px);box-shadow:0 20px 40px #00000066;overflow:hidden;}
    .head{padding:22px;border-bottom:1px solid var(--line);display:flex;gap:10px;flex-wrap:wrap;justify-content:space-between;align-items:center;}
    .title{font-size:1.2rem;font-weight:700;letter-spacing:.3px}.sub{color:var(--muted);font-size:.92rem}
    .grid{display:grid;grid-template-columns:360px minmax(0,1fr);gap:16px;padding:16px;align-items:start}.stack{display:grid;gap:16px}
    .panel{border:1px solid var(--line);border-radius:16px;padding:14px;background:var(--panel)}
    .panel h3{margin:0 0 12px 0;font-size:1rem}
    .status{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;font-size:.92rem}
    .chip{border:1px solid var(--line);border-radius:10px;padding:8px 10px;background:#0f172a}.chip b{display:block;color:#f8fafc}
    .buttons{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px}
    .config-row,.wifi-row{display:flex;gap:8px;margin:12px 0 10px;flex-wrap:wrap}
    .config-row input,.wifi-row input,.wifi-row select{flex:1;min-width:170px;border:1px solid #475569;border-radius:10px;background:#0f172a;color:var(--text);padding:10px 12px;font-size:.9rem}
    button{border:0;border-radius:12px;padding:13px 14px;font-size:.95rem;font-weight:700;color:white;cursor:pointer;transition:transform .08s ease, filter .15s ease;background:linear-gradient(135deg,#0ea5e9,#2563eb)}
    button:hover{filter:brightness(1.08)}button:active{transform:scale(.98)}.b-on{background:linear-gradient(135deg,#16a34a,#22c55e)}.b-off{background:linear-gradient(135deg,#f59e0b,#ea580c)}.b-reboot{background:linear-gradient(135deg,#7c3aed,#4c1d95)}.b-reset{background:linear-gradient(135deg,#ef4444,#b91c1c)}.b-save{background:linear-gradient(135deg,#0f766e,#14b8a6)}.b-pulse{background:linear-gradient(135deg,#0284c7,#2563eb)}.b-ota{background:linear-gradient(135deg,#0891b2,#0e7490)}
    .api-note,.helper{color:var(--muted);font-size:.82rem;line-height:1.5}
    .result{margin-top:10px;font-size:.85rem;color:#cbd5e1;min-height:1.2em;font-family:ui-monospace,SFMono-Regular,Menlo,monospace}
    .buglog-layout{display:grid;grid-template-columns:minmax(0,1fr);gap:12px;align-items:start;margin-top:10px}
    .legend{border:1px solid var(--line);border-radius:12px;padding:10px 12px;background:#0f172a;color:#cbd5e1;font-size:.8rem;line-height:1.5}
    .legend b{color:#f8fafc}
    .legend code{color:#e2e8f0}
    .instructions{display:grid;gap:8px;font-size:.88rem;color:#dbe4ef;line-height:1.5}.instructions div{border-left:3px solid #0ea5e9;padding-left:10px}
    .api-box{margin:0 16px 16px;padding:14px;border:1px solid var(--line);border-radius:14px;background:var(--panel)}
    .api-list{display:grid;gap:8px;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:.82rem;color:#cbd5e1;word-break:break-all}
    .modal-backdrop{position:fixed;inset:0;background:#020617b3;display:none;align-items:center;justify-content:center;padding:16px;z-index:50}.modal-backdrop.show{display:flex}
    .modal{width:min(560px,100%);background:#0b1220;border:1px solid var(--line);border-radius:18px;padding:18px;box-shadow:0 24px 48px #00000088}.modal h3{margin:0 0 10px 0;font-size:1rem}.modal p{margin:0;color:#cbd5e1;line-height:1.5;font-size:.92rem}.modal-actions{display:flex;gap:10px;justify-content:flex-end;margin-top:16px;flex-wrap:wrap}.b-cancel{background:linear-gradient(135deg,#64748b,#334155)}
    @media (max-width:980px){.grid{grid-template-columns:1fr}.buttons{grid-template-columns:repeat(2,minmax(0,1fr))}.buglog-layout{grid-template-columns:1fr}}
    @media (max-width:760px){.buttons{grid-template-columns:1fr}.status{grid-template-columns:1fr}}
  </style>
</head>
<body>
  <div class="wrap">
    <div class="head"><div><div class="title">Power Console</div><div class="sub">PC power, test mode e OTA locale</div></div><div class="sub" id="ipLine">IP: ...</div></div>
    <div class="grid">
      <div class="stack">
        <div class="panel"><h3>Stato</h3><div class="status"><div class="chip">Mode<b id="mode">-</b></div><div class="chip">Out<b id="output">-</b></div><div class="chip">RSSI<b id="rssi">-</b></div><div class="chip">Segnale<b id="signal">-</b></div><div class="chip">Ora<b id="romeTime">-</b></div><div class="chip">Up<b id="uptime">-</b></div><div class="chip">FW<b id="fwVersion">-</b></div><div class="chip">Build<b id="fwBuild">-</b></div><div class="chip">Slot<b id="fwSlot">-</b></div><div class="chip">Chip<b id="chipModel">-</b></div><div class="chip">Reset<b id="resetReason">-</b></div><div class="chip">Ultimo trigger<b id="last">-</b></div></div><div class="helper" id="extraState" style="margin-top:10px">-</div></div>
        <div class="panel"><h3>Istruzioni rapide</h3><div class="instructions"><div><b>PULSE</b> invia un impulso breve al tasto power del PC.</div><div><b>FORCE SHUTDOWN</b> invia una pressione lunga. Usalo solo se il sistema non risponde.</div><div><b>TEST ON</b> tiene il pin attivo per diagnostica. Il firmware lo disattiva automaticamente dopo 30 minuti.</div><div><b>RESET WIFI</b> cancella le credenziali e riporta la scheda in AP mode.</div><div><b>WEB OTA</b> apre la pagina di aggiornamento. Carica solo il firmware .bin principale della stessa scheda.</div><div><b>RECOVERY</b> usa <code>GET /api/boot-recovery</code> per riavviare la board in partizione factory.</div></div></div>
      </div>
      <div class="stack">
        <div class="panel"><h3>Comandi</h3><div class="config-row"><input id="pulseMsInput" type="number" min="50" step="10" placeholder="Pulse ms"><input id="forceMsInput" type="number" min="500" step="10" placeholder="Force ms"><button class="b-save" onclick="applyTimingsToDevice()">Salva</button></div><div class="buttons"><button id="pulseBtn" class="b-pulse" onclick="cmdPulse()">PULSE</button><button id="forceBtn" class="b-reset" onclick="cmdForceShutdown()">FORCE</button><button class="b-on" onclick="cmdTestOn()">TEST ON</button><button class="b-off" onclick="cmdTestOff()">TEST OFF</button><button class="b-reboot" onclick="cmdReboot()">REBOOT ESP</button><button class="b-ota" onclick="cmdOpenOta()">WEB OTA</button><button class="b-reset" onclick="cmdResetWifi()">RESET WIFI</button></div><div class="result" id="cmdResult">Pronto</div></div>
        <div class="panel"><h3>WiFi</h3><div class="wifi-row"><select id="wifiSelect" onchange="pickScannedWifi()"><option value="">Rete trovata</option></select><button class="b-save" onclick="scanWifiNetworks()">Scansiona</button></div><div class="wifi-row"><input id="wifiSsidInput" type="text" placeholder="SSID"><input id="wifiPassInput" type="password" placeholder="Password"><button class="b-save" onclick="applyWifiToDevice()">Salva</button></div><div class="api-note" id="wifiScanInfo">Le credenziali WiFi vengono inviate con POST e il reboot resta manuale.</div></div>
        <div class="panel"><h3>Log RAM</h3><div class="config-row"><button class="b-save" onclick="refreshLogs()">Aggiorna</button><button class="b-save" onclick="copyLogsToClipboard()">Copy</button><button class="b-reset" onclick="clearLogs()">Pulisci</button></div><div class="api-note">Questi log sono volatili e si perdono al reboot o al power cycle.</div><div class="result" id="logBox" style="white-space:pre-wrap;max-height:220px;overflow:auto">Nessun log.</div></div>
        <div class="panel"><h3>Buglog Persistente</h3><div class="config-row"><button class="b-save" onclick="refreshBuglog()">Aggiorna</button><button class="b-save" onclick="copyBuglogToClipboard()">Copy</button><button class="b-reset" onclick="clearBuglog()">Pulisci</button></div><div class="api-note">Salvato in flash locale. Resta disponibile anche dopo reset e power cycle.</div><div class="buglog-layout"><div class="result" id="buglogBox" style="white-space:pre-wrap;max-height:220px;overflow:auto">Nessun buglog.</div><div class="legend"><div><b>Severity</b>: <code>INFO</code> = normale, <code>WARN</code> = anomalia, <code>ERROR</code> = errore, <code>FATAL</code> = reboot watchdog.</div><div style="margin-top:8px"><b>Code</b>: <code>1000</code> boot, <code>1001</code> crash reset, <code>1101</code> watchdog reboot, <code>1102</code> recovery/factory, <code>1201</code> reset WiFi, <code>1301</code>/<code>1302</code> task create fail, <code>1310</code> task recovery, <code>1311</code> task recovery con reset offset, <code>1312</code> failsafe Telegram, <code>1313</code> heap_guard richiesto, <code>1500</code> heartbeat ok, <code>1501</code> heartbeat anomalo, <code>1600</code> tg_poll, <code>1601</code> tg_send, <code>1602</code> tg_recovery, <code>1603</code> status_json, <code>1604</code> web_logs, <code>1605</code> wifi_scan, <code>1606</code> tg_status_msg, <code>1610</code>/<code>1611</code>/<code>1612</code>/<code>1613</code>/<code>1614</code> boot_probe, <code>1620</code> boot_cfg snapshot, <code>1621</code> boot_wifi snapshot, <code>1622</code> boot_ntp snapshot, <code>1623</code> boot_http snapshot, <code>1624</code> boot_tasks snapshot, <code>1625</code> tg_recovery snapshot, <code>1626</code> tg_send snapshot, <code>1627</code> tg_poll snapshot.</div><div style="margin-top:8px"><b>Heartbeat</b>: ogni 15 minuti se tutto e normale, ogni 2 minuti se rileva una condizione sospetta.</div><div style="margin-top:8px"><b>Campi heartbeat</b>: <code>fh</code> = free heap disponibile, <code>mb</code> = blocco heap contiguo massimo allocabile, <code>wf</code> = WiFi connesso (<code>1</code>) o non connesso (<code>0</code>), <code>la</code> = eta ultimo heartbeat del loop principale in ms, <code>ta</code> = eta ultimo heartbeat del task Telegram in ms.</div><div style="margin-top:8px"><b>Campi probe heap</b>: formato <code>tag beforeFree/beforeMax&gt;afterFree/afterMax</code>. Esempio <code>tg_poll 68024/57332&gt;15980/7668</code> = il polling Telegram e uscito con heap molto piu basso di come era entrato.</div><div style="margin-top:8px"><b>Campi snapshot</b>: formato <code>tag n=... fh=... mb=...</code>. Servono a mostrare lo stato heap subito dopo recovery/send/poll e a fine fase boot.</div><div style="margin-top:8px"><b>Heap guard</b>: se <code>fh</code> scende sotto 22 KB o <code>mb</code> sotto 16 KB, il task Telegram si mette in pausa breve e il <code>loop()</code> prova un recovery controllato.</div></div></div></div>
      </div>
    </div>
<div class="api-box"><h3>API</h3><div class="api-list"><div id="apiStatusLine">GET /api/status</div><div id="apiPulseLine">GET /api/pc/pulse</div><div id="apiForceLine">GET /api/pc/forceshutdown</div><div id="apiTestOnLine">GET /api/pc/test/on</div><div id="apiTestOffLine">GET /api/pc/test/off</div><div id="apiTimingsLine">GET /api/config/timings?pulse_ms=500&amp;force_ms=3000</div><div id="apiWifiScanLine">GET /api/wifi/scan</div><div id="apiWifiLine">POST /api/config/wifi (body: ssid=SSID&amp;password=PASSWORD)</div><div id="apiLogsLine">GET /api/logs?limit=30</div><div id="apiLogsClearLine">GET /api/logs/clear</div><div id="apiBuglogLine">GET /api/buglog</div><div id="apiBuglogClearLine">GET /api/buglog/clear</div><div id="apiBootHistoryLine">GET /api/boot-history?limit=12</div><div id="apiRebootLine">GET /api/reboot</div><div id="apiBootRecoveryLine">GET /api/boot-recovery</div><div id="apiTelegramHealthLine">GET /api/telegram/health</div><div id="apiTelegramRecoverLine">GET /api/telegram/recover?reset_offset=1</div></div></div>
  </div>
  <div class="modal-backdrop" id="modal"><div class="modal"><h3 id="modalTitle">Conferma</h3><p id="modalText"></p><div class="modal-actions"><button class="b-cancel" onclick="closeModal()">Annulla</button><button class="b-save" onclick="confirmModalAction()">OK</button></div></div></div>
  <script>
    const baseUrl = () => `${location.protocol}//${location.host}`;
    let pendingWifiConfig = null;
    let pendingAction = null;
    let wifiFormDirty = false;
    let timingFormDirty = false;
    function setResult(msg){ document.getElementById('cmdResult').textContent = msg; }
    function openConfirmModal(title, text, action){ document.getElementById('modalTitle').textContent = title; document.getElementById('modalText').textContent = text; pendingAction = action || null; document.getElementById('modal').classList.add('show'); }
    function closeModal(clearWifiFields = false){ pendingAction = null; if(clearWifiFields){ pendingWifiConfig = null; document.getElementById('wifiSsidInput').value = ''; document.getElementById('wifiPassInput').value = ''; } document.getElementById('modal').classList.remove('show'); }
    function confirmModalAction(){ const action = pendingAction; if(!action) return; action(); }
    function pickScannedWifi(){ const selected = document.getElementById('wifiSelect').value; if(selected){ document.getElementById('wifiSsidInput').value = selected; wifiFormDirty = true; } }
    function refreshApiLegend(){ document.getElementById('apiStatusLine').textContent = `GET ${baseUrl()}/api/status`; document.getElementById('apiPulseLine').textContent = `GET ${baseUrl()}/api/pc/pulse`; document.getElementById('apiForceLine').textContent = `GET ${baseUrl()}/api/pc/forceshutdown`; document.getElementById('apiTestOnLine').textContent = `GET ${baseUrl()}/api/pc/test/on`; document.getElementById('apiTestOffLine').textContent = `GET ${baseUrl()}/api/pc/test/off`; document.getElementById('apiTimingsLine').textContent = `GET ${baseUrl()}/api/config/timings?pulse_ms=500&force_ms=3000`; document.getElementById('apiWifiScanLine').textContent = `GET ${baseUrl()}/api/wifi/scan`; document.getElementById('apiWifiLine').textContent = `POST ${baseUrl()}/api/config/wifi  body: ssid=SSID&password=PASSWORD`; document.getElementById('apiLogsLine').textContent = `GET ${baseUrl()}/api/logs?limit=30`; document.getElementById('apiLogsClearLine').textContent = `GET ${baseUrl()}/api/logs/clear`; document.getElementById('apiBuglogLine').textContent = `GET ${baseUrl()}/api/buglog`; document.getElementById('apiBuglogClearLine').textContent = `GET ${baseUrl()}/api/buglog/clear`; document.getElementById('apiBootHistoryLine').textContent = `GET ${baseUrl()}/api/boot-history?limit=12`; document.getElementById('apiRebootLine').textContent = `GET ${baseUrl()}/api/reboot`; document.getElementById('apiBootRecoveryLine').textContent = `GET ${baseUrl()}/api/boot-recovery`; document.getElementById('apiTelegramHealthLine').textContent = `GET ${baseUrl()}/api/telegram/health`; document.getElementById('apiTelegramRecoverLine').textContent = `GET ${baseUrl()}/api/telegram/recover?reset_offset=1`; }
async function scanWifiNetworks(){ const info = document.getElementById('wifiScanInfo'); const select = document.getElementById('wifiSelect'); info.textContent = 'Scan...'; select.innerHTML = '<option value="">Rete trovata</option>'; try{ for(let attempt = 0; attempt < 30; attempt++){ const r = await fetch('/api/wifi/scan', {cache:'no-store'}); const j = await r.json(); if(j.scanning){ info.textContent = 'Scan in corso...'; await new Promise((resolve) => setTimeout(resolve, 500)); continue; } (j.networks || []).forEach((n) => { const opt = document.createElement('option'); opt.value = n.ssid; opt.textContent = `${n.ssid} (${n.signal_pct}%, ${n.rssi} dBm${n.open ? ', open' : ''})`; select.appendChild(opt); }); info.textContent = `${j.count || 0} reti trovate`; return; } info.textContent = 'Scan timeout'; } catch(e) { info.textContent = 'Scan KO'; } }
    function updateStatusView(j){ document.getElementById('ipLine').textContent = `IP: ${j.ip}`; document.getElementById('mode').textContent = j.mode; document.getElementById('output').textContent = j.output_high ? 'HIGH' : 'LOW'; document.getElementById('rssi').textContent = `${j.rssi} dBm`; document.getElementById('signal').textContent = `${j.signal_pct}%`; document.getElementById('romeTime').textContent = j.rome_time; document.getElementById('uptime').textContent = j.uptime_human; document.getElementById('fwVersion').textContent = j.fw_version; document.getElementById('fwBuild').textContent = j.fw_build; document.getElementById('fwSlot').textContent = j.running_partition; document.getElementById('chipModel').textContent = j.chip_model; document.getElementById('resetReason').textContent = j.reset_reason; document.getElementById('last').textContent = j.last_trigger; const modeBits = []; modeBits.push(j.wifi_connected ? 'WiFi connected' : 'WiFi offline'); if(j.ap_mode) modeBits.push('AP mode active'); if(j.telegram_confirm_pending) modeBits.push('Telegram confirm pending'); if(j.test_timeout_remaining_s > 0) modeBits.push(`TEST timeout ${j.test_timeout_remaining_s}s`); modeBits.push(`Logs ${j.log_count}`); document.getElementById('extraState').textContent = modeBits.join(' | ') || '-'; if(!timingFormDirty){ document.getElementById('pulseMsInput').value = j.pulse_ms; document.getElementById('forceMsInput').value = j.force_ms; } if(!wifiFormDirty){ document.getElementById('wifiSsidInput').value = j.wifi_configured_ssid || ''; } document.getElementById('pulseBtn').textContent = `PULSE ${j.pulse_ms} ms`; document.getElementById('forceBtn').textContent = `FORCE SHUTDOWN ${j.force_ms} ms`; }
    async function refreshLogs(){ try{ const r = await fetch('/api/logs?limit=30', {cache:'no-store'}); const j = await r.json(); document.getElementById('logBox').textContent = (j.logs && j.logs.length) ? j.logs.join('\n') : 'Nessun log.'; } catch(e){ document.getElementById('logBox').textContent = 'Log non disponibili.'; } }
    async function clearLogs(){ const r = await fetch('/api/logs/clear', {method:'GET'}); const t = await r.text(); setResult(`/api/logs/clear ${r.status} ${t.substring(0,90)}`); setTimeout(refreshLogs,120); setTimeout(refreshStatus,120); }
    async function copyLogsToClipboard(){ const text = document.getElementById('logBox').textContent || ''; if(!text || text === 'Nessun log.' || text === 'Log non disponibili.'){ setResult('Nessun log da copiare'); return; } try{ if(navigator.clipboard && window.isSecureContext){ await navigator.clipboard.writeText(text); } else { const ta = document.createElement('textarea'); ta.value = text; ta.style.position = 'fixed'; ta.style.left = '-9999px'; document.body.appendChild(ta); ta.focus(); ta.select(); document.execCommand('copy'); document.body.removeChild(ta); } setResult('Log copiati negli appunti'); } catch(e){ setResult('Copy non riuscito'); } }
    async function refreshBuglog(){ try{ const r = await fetch('/api/buglog', {cache:'no-store'}); const t = await r.text(); document.getElementById('buglogBox').textContent = t && t.trim().length ? t.trim() : 'Nessun buglog.'; } catch(e){ document.getElementById('buglogBox').textContent = 'Buglog non disponibile.'; } }
    async function clearBuglog(){ const r = await fetch('/api/buglog/clear', {method:'GET'}); const t = await r.text(); setResult(`/api/buglog/clear ${r.status} ${t.substring(0,90)}`); setTimeout(refreshBuglog,120); }
    async function copyBuglogToClipboard(){ const text = document.getElementById('buglogBox').textContent || ''; if(!text || text === 'Nessun buglog.' || text === 'Buglog non disponibile.'){ setResult('Nessun buglog da copiare'); return; } try{ if(navigator.clipboard && window.isSecureContext){ await navigator.clipboard.writeText(text); } else { const ta = document.createElement('textarea'); ta.value = text; ta.style.position = 'fixed'; ta.style.left = '-9999px'; document.body.appendChild(ta); ta.focus(); ta.select(); document.execCommand('copy'); document.body.removeChild(ta); } setResult('Buglog copiato negli appunti'); } catch(e){ setResult('Copy non riuscito'); } }
    async function refreshStatus(){ try{ const r = await fetch('/api/status', {cache:'no-store'}); const j = await r.json(); updateStatusView(j); } catch(e){} }
    async function hit(path){ const r = await fetch(path, {method:'GET'}); const t = await r.text(); setResult(`${path} ${r.status} ${t.substring(0,90)}`); setTimeout(refreshStatus, 120); }
    async function applyTimingsToDevice(){ const nextPulse = parseInt(document.getElementById('pulseMsInput').value || '0', 10); const nextForce = parseInt(document.getElementById('forceMsInput').value || '0', 10); if(nextPulse < 50 || nextForce < 500){ setResult('Tempi non validi'); return; } const url = `/api/config/timings?pulse_ms=${nextPulse}&force_ms=${nextForce}`; const r = await fetch(url, {method:'GET'}); const t = await r.text(); if(r.ok) timingFormDirty = false; setResult(`/api/config/timings ${r.status} ${t.substring(0,90)}`); setTimeout(refreshStatus, 120); }
    function applyWifiToDevice(){ const ssid = document.getElementById('wifiSsidInput').value.trim(); const password = document.getElementById('wifiPassInput').value; if(!ssid){ setResult('SSID richiesto'); return; } pendingWifiConfig = { ssid, password }; openConfirmModal('Nuovo WiFi', `Conferma "${ssid}". Le credenziali saranno salvate e il reboot restera manuale.`, confirmWifiSave); }
    async function confirmWifiSave(){ if(!pendingWifiConfig) return; const { ssid, password } = pendingWifiConfig; const body = new URLSearchParams(); body.set('ssid', ssid); body.set('password', password); closeModal(false); const r = await fetch('/api/config/wifi', { method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8'}, body: body.toString() }); const t = await r.text(); if(r.ok){ wifiFormDirty = false; } setResult(`/api/config/wifi ${r.status} ${t.substring(0,100)}`); setTimeout(refreshStatus, 120); }
    function cmdPulse(){ hit('/api/pc/pulse'); }
    function cmdTestOn(){ openConfirmModal('Conferma TEST ON', 'TEST ON mantiene il pin power attivo per diagnostica. Timeout automatico dopo 30 minuti.', () => { closeModal(); hit('/api/pc/test/on'); }); }
    function cmdTestOff(){ hit('/api/pc/test/off'); }
    function cmdReboot(){ openConfirmModal('Conferma reboot', "Riavviare l'ESP? L'uscita power verra prima rilasciata.", () => { closeModal(); hit('/api/reboot'); }); }
    function cmdForceShutdown(){ openConfirmModal('Conferma force shutdown', 'Inviare un force shutdown prolungato al PC?', () => { closeModal(); hit('/api/pc/forceshutdown'); }); }
    function cmdOpenOta(){ openConfirmModal('Apri Web OTA', 'Aprire la pagina Web OTA? Se eri in TEST ON, disattivalo prima.', () => { closeModal(); location.href = '/update'; }); }
    function cmdResetWifi(){ openConfirmModal('Conferma reset WiFi', 'Cancellare il WiFi e riavviare? La scheda tornera in AP mode.', () => { closeModal(); location.href = '/resetwifi'; }); }
    document.addEventListener('DOMContentLoaded', () => { document.getElementById('wifiSsidInput').addEventListener('input', () => { wifiFormDirty = true; }); document.getElementById('wifiPassInput').addEventListener('input', () => { wifiFormDirty = true; }); document.getElementById('pulseMsInput').addEventListener('input', () => { timingFormDirty = true; }); document.getElementById('forceMsInput').addEventListener('input', () => { timingFormDirty = true; }); });
    refreshApiLegend(); refreshStatus(); refreshLogs(); refreshBuglog(); setInterval(refreshStatus, 3000); setInterval(refreshLogs, 9000); setInterval(refreshBuglog, 15000);
  </script>
</body>
</html>
)HTML";

const char UPDATE_HTML[] PROGMEM =
R"HTML(
<!doctype html>
<html lang="it">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Power Console OTA</title>
)HTML"
"  <link rel=\"icon\" type=\"image/svg+xml\" href=\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'%3E%3Crect width='64' height='64' rx='16' fill='%230f172a'/%3E%3Cpath d='M32 12v18' stroke='%23f8fafc' stroke-width='6' stroke-linecap='round'/%3E%3Cpath d='M21 20a16 16 0 1 0 22 0' fill='none' stroke='%2338bdf8' stroke-width='6' stroke-linecap='round'/%3E%3C/svg%3E\">\n"
R"HTML(
  <style>
    :root{--bg:#0f172a;--bg2:#1e293b;--card:#111827cc;--text:#e5e7eb;--muted:#94a3b8;--line:#334155;}
    *{box-sizing:border-box}
    body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px;font-family:"Segoe UI",Tahoma,sans-serif;color:var(--text);background:radial-gradient(circle at 20% 15%, #164e63 0%, transparent 35%),radial-gradient(circle at 80% 85%, #3f1d2e 0%, transparent 35%),linear-gradient(135deg, var(--bg), var(--bg2));}
    .card{width:min(760px,100%);background:var(--card);border:1px solid var(--line);border-radius:20px;padding:22px;box-shadow:0 20px 40px #00000066}
    h1{margin:0 0 10px;font-size:1.2rem} p,li{color:var(--muted);line-height:1.5} ul{margin:12px 0 0 18px;padding:0}
    .box{margin-top:16px;padding:14px;border:1px solid var(--line);border-radius:14px;background:#0b1220aa}
    input[type=file]{width:100%;padding:10px;border:1px solid #475569;border-radius:10px;background:#0f172a;color:#e5e7eb}
    button{margin-top:12px;border:0;border-radius:12px;padding:12px 16px;font-size:.95rem;font-weight:700;color:white;cursor:pointer;background:linear-gradient(135deg,#0f766e,#14b8a6)}
    .secondary{background:linear-gradient(135deg,#64748b,#334155);margin-left:8px} progress{width:100%;margin-top:12px}
    .status{margin-top:10px;font-family:ui-monospace,SFMono-Regular,Menlo,monospace;color:#cbd5e1;min-height:1.2em}
  </style>
</head>
<body>
  <div class="card">
    <h1>Web OTA</h1>
    <p>Carica solo il file <code>.bin</code> principale compilato per questa stessa scheda. Prima dell'aggiornamento conviene disattivare eventuale TEST ON.</p>
    <ul><li>Non staccare alimentazione durante upload e reboot.</li><li>Non usare bootloader o partitions in questa pagina.</li><li>Dopo HTTP 200 il dispositivo si riavvia da solo.</li></ul>
    <div class="box"><input id="binFile" type="file" accept=".bin,application/octet-stream"><button onclick="uploadFirmware()">Carica firmware</button><button class="secondary" onclick="location.href='/'">Torna alla console</button><progress id="prog" value="0" max="100" style="display:none"></progress><div class="status" id="status">Pronto</div></div>
  </div>
  <script>
    function setStatus(msg){ document.getElementById('status').textContent = msg; }
    let otaPollTimer = null;
    function stopOtaPolling(){ if(otaPollTimer){ clearInterval(otaPollTimer); otaPollTimer = null; } }
    async function pollOtaProgress(){
      try{
        const r = await fetch(`/api/update/progress?ts=${Date.now()}`, {cache:'no-store'});
        if(!r.ok) return;
        const j = await r.json();
        const prog = document.getElementById('prog');
        prog.style.display = 'block';
        if(typeof j.pct === 'number' && j.pct >= 0){
          prog.value = Math.max(prog.value || 0, Math.min(100, j.pct));
        }
        const w = Math.round((j.written || 0) / 1024);
        const e = Math.round((j.expected || 0) / 1024);
        if(j.in_progress){
          if(e > 0) setStatus(`Scrittura flash ${prog.value}% (${w}/${e} KB)`);
          else setStatus(`Scrittura flash ${w} KB`);
        } else if(j.finished){
          if(j.success){
            prog.value = 100;
            setStatus('Firmware scritto. Riavvio in corso...');
          } else {
            setStatus(`Errore OTA: ${j.status || 'failed'}`);
          }
          stopOtaPolling();
        }
      } catch(e){}
    }
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
      stopOtaPolling();
      setStatus('Upload rete in corso...');
      xhr.open('POST', '/api/update');
      xhr.setRequestHeader('X-File-Size', String(file.size));
      xhr.upload.onprogress = (e) => {
        if(!e.lengthComputable) return;
        const pct = Math.round((e.loaded / e.total) * 100);
        prog.value = Math.max(prog.value || 0, Math.min(100, pct));
        setStatus(`Upload rete ${pct}%`);
      };
      xhr.upload.onload = () => {
        setStatus('Upload completato, scrittura flash...');
        otaPollTimer = setInterval(pollOtaProgress, 400);
      };
      xhr.onload = () => {
        stopOtaPolling();
        if(xhr.status === 200){
          prog.value = 100;
          setStatus('Aggiornamento completato. Riavvio in corso...');
        } else {
          setStatus(`HTTP ${xhr.status}: ${xhr.responseText || 'errore'}`);
        }
      };
      xhr.onerror = () => { stopOtaPolling(); setStatus('Upload fallito'); };
      xhr.send(formData);
    }
  </script>
</body>
</html>
)HTML";

void handleRoot() {
  telegramBumpWebActivity();
  addNoCacheHeaders();
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleUpdatePage() {
  telegramBumpWebActivity();
  addNoCacheHeaders();
  server.send_P(200, "text/html", UPDATE_HTML);
}

void handleResetWiFi() {
  if (!guardTextRateLimit(webSensitiveBucket, WEB_SENSITIVE_WINDOW_MS, WEB_SENSITIVE_MAX_HITS, WEB_SENSITIVE_MIN_GAP_MS, "Too many requests")) return;
  releasePcOutputForSensitiveAction("reset-wifi");
  logLine("[WiFi] Reset credenziali richiesto");
  server.send(200, "text/plain", "Reset credenziali WiFi. Riavvio...");
  delay(600);
  wm.resetSettings();
  if (lockPrefs()) {
    prefs.begin("pcpower", false);
    prefs.remove("wifi_ssid");
    prefs.remove("wifi_pass");
    prefs.end();
    unlockPrefs();
  } else {
    logLine("[WiFi] Reset credenziali: mutex prefs occupato");
  }
  log_event(BUGLOG_WARN, 1201, "wifi_credentials_reset");
  buglog_flush(8);
  markIntentionalReboot();
  ESP.restart();
}

void handleApiStatus() { sendJson(200, statusJson()); }

String telegramHealthJson() {
  unsigned long now = millis();
  unsigned long pollAge = telegramLastPollOkMs > 0 ? (now - telegramLastPollOkMs) : 0;
  unsigned long rxAge = telegramLastReceiveMs > 0 ? (now - telegramLastReceiveMs) : 0;
  unsigned long sendAge = telegramLastSendOkMs > 0 ? (now - telegramLastSendOkMs) : 0;
  unsigned long heartbeatAge = telegramHeartbeatMs > 0 ? (now - telegramHeartbeatMs) : 0;
  unsigned long loopAge = loopHeartbeatMs > 0 ? (now - loopHeartbeatMs) : 0;

  String json = "{";
  json += "\"ok\":true,";
  json += "\"wifi_connected\":" + String(wifiConnected() ? "true" : "false") + ",";
  json += "\"configured\":" + String(telegramConfigured() ? "true" : "false") + ",";
  json += "\"task_running\":" + String(telegramTaskHandle != nullptr ? "true" : "false") + ",";
  json += "\"in_backoff\":" + String(telegramInBackoff() ? "true" : "false") + ",";
  json += "\"in_failsafe\":" + String(telegramInFailsafePause() ? "true" : "false") + ",";
  json += "\"last_http_code\":" + String(tgLastHttpCode) + ",";
  json += "\"last_error\":\"" + jsonEscape(getTgLastError()) + "\",";
  json += "\"task_restart_count\":" + String(telegramTaskRestartCount) + ",";
  json += "\"last_update_id\":" + String(telegramLastUpdateId) + ",";
  json += "\"heap_recovery_pending\":" + String(telegramHeapRecoveryRequested ? "true" : "false") + ",";
  json += "\"heap_recovery_free_heap\":" + String(telegramHeapRecoveryFreeHeap) + ",";
  json += "\"heap_recovery_max_block\":" + String(telegramHeapRecoveryMaxBlock) + ",";
  json += "\"webhook_recovery_pending\":" + String(telegramWebhookRecoveryPending ? "true" : "false") + ",";
  json += "\"poll_ok_count\":" + String(telegramMetricPollOk) + ",";
  json += "\"poll_err_count\":" + String(telegramMetricPollKo) + ",";
  json += "\"poll_parse_err_count\":" + String(telegramMetricPollParseKo) + ",";
  json += "\"updates_rx_count\":" + String(telegramMetricUpdatesRx) + ",";
  json += "\"commands_rx_count\":" + String(telegramMetricCommandsRx) + ",";
  json += "\"send_ok_count\":" + String(telegramMetricSendOk) + ",";
  json += "\"send_err_count\":" + String(telegramMetricSendKo) + ",";
  json += "\"cmd_rate_drop_count\":" + String(telegramMetricCmdRateDrops) + ",";
  json += "\"webhook_recovery_count\":" + String(telegramMetricWebhookRecoveries) + ",";
  json += "\"poll_age_ms\":" + String(pollAge) + ",";
  json += "\"last_rx_age_ms\":" + String(rxAge) + ",";
  json += "\"last_send_ok_age_ms\":" + String(sendAge) + ",";
  json += "\"telegram_heartbeat_age_ms\":" + String(heartbeatAge) + ",";
  json += "\"loop_heartbeat_age_ms\":" + String(loopAge);
  json += "}";
  return json;
}

void handleApiTelegramHealth() { sendJson(200, telegramHealthJson()); }

void handleApiTelegramRecover() {
  if (!guardJsonRateLimit(webSensitiveBucket, WEB_SENSITIVE_WINDOW_MS, WEB_SENSITIVE_MAX_HITS, WEB_SENSITIVE_MIN_GAP_MS, "rate_limited")) return;
  bool resetOffset = server.hasArg("reset_offset") && server.arg("reset_offset") != "0";
  bool ok = restartTelegramTask(resetOffset ? "manual_api_reset_offset" : "manual_api", resetOffset);
  String json = "{";
  json += "\"ok\":" + String(ok ? "true" : "false") + ",";
  json += "\"reset_offset\":" + String(resetOffset ? "true" : "false") + ",";
  json += "\"task_running\":" + String(telegramTaskHandle != nullptr ? "true" : "false") + ",";
  json += "\"task_restart_count\":" + String(telegramTaskRestartCount) + ",";
  json += "\"last_update_id\":" + String(telegramLastUpdateId);
  json += "}";
  sendJson(ok ? 200 : 500, json);
}

void handleApiBootHistory() {
  int limit = server.hasArg("limit") ? server.arg("limit").toInt() : BOOT_HISTORY_CAPACITY;
  sendJson(200, bootHistoryJson(limit));
}

void handleApiWifiScan() {
  if (!guardJsonRateLimit(webWifiScanBucket, WEB_WIFI_SCAN_WINDOW_MS, WEB_WIFI_SCAN_MAX_HITS, WEB_WIFI_SCAN_MIN_GAP_MS, "scan_rate_limited")) return;
  sendJson(200, wifiScanJson());
}
void handleApiLogs() {
  int limit = server.hasArg("limit") ? server.arg("limit").toInt() : 30;
  sendJson(200, webLogsJson(limit));
}

void handleApiBugLogDump() {
  addNoCacheHeaders();
  buglog_flush(8);
  server.send(200, "text/plain", buglog_dump_string());
}

void handleApiBugLogClear() {
  if (!guardJsonRateLimit(webSensitiveBucket, WEB_SENSITIVE_WINDOW_MS, WEB_SENSITIVE_MAX_HITS, WEB_SENSITIVE_MIN_GAP_MS, "rate_limited")) return;
  if (!log_clear()) {
    sendJson(500, "{\"ok\":false,\"error\":\"buglog_clear_failed\"}");
    return;
  }
  sendJson(200, "{\"ok\":true,\"message\":\"buglog_cleared\"}");
}

void handleApiLogsClear() {
  if (!guardJsonRateLimit(webSensitiveBucket, WEB_SENSITIVE_WINDOW_MS, WEB_SENSITIVE_MAX_HITS, WEB_SENSITIVE_MIN_GAP_MS, "rate_limited")) return;
  clearWebLogs();
  appendWebLog("[SYS] Web log buffer pulito");
  sendJson(200, "{\"ok\":true,\"message\":\"logs_cleared\"}");
}

void handleApiPulse() {
  if (!guardJsonRateLimit(webSensitiveBucket, WEB_SENSITIVE_WINDOW_MS, WEB_SENSITIVE_MAX_HITS, WEB_SENSITIVE_MIN_GAP_MS, "rate_limited")) return;
  if (requestPulse("http-api")) sendJson(200, "{\"ok\":true,\"mode\":\"pulse\",\"pulse_ms\":" + String(pulseMs) + "}");
  else sendJson(429, "{\"ok\":false,\"error\":\"busy_or_lockout\"}");
}

void handleApiTestOn() {
  if (!guardJsonRateLimit(webSensitiveBucket, WEB_SENSITIVE_WINDOW_MS, WEB_SENSITIVE_MAX_HITS, WEB_SENSITIVE_MIN_GAP_MS, "rate_limited")) return;
  setTestOutput(true, "http-test-on");
  sendJson(200, "{\"ok\":true,\"mode\":\"test_on\"}");
}

void handleApiTestOff() {
  if (!guardJsonRateLimit(webSensitiveBucket, WEB_SENSITIVE_WINDOW_MS, WEB_SENSITIVE_MAX_HITS, WEB_SENSITIVE_MIN_GAP_MS, "rate_limited")) return;
  setTestOutput(false, "http-test-off");
  sendJson(200, "{\"ok\":true,\"mode\":\"test_off\"}");
}

void handleApiSetTimings() {
  if (!guardJsonRateLimit(webSensitiveBucket, WEB_SENSITIVE_WINDOW_MS, WEB_SENSITIVE_MAX_HITS, WEB_SENSITIVE_MIN_GAP_MS, "rate_limited")) return;
  if (!server.hasArg("pulse_ms") || !server.hasArg("force_ms")) {
    sendJson(400, "{\"ok\":false,\"error\":\"missing_pulse_or_force_ms\"}");
    return;
  }

  unsigned long nextPulse = strtoul(server.arg("pulse_ms").c_str(), nullptr, 10);
  unsigned long nextForce = strtoul(server.arg("force_ms").c_str(), nullptr, 10);

  if (nextPulse < 50 || nextPulse > 10000 || nextForce < 500 || nextForce > 15000) {
    sendJson(400, "{\"ok\":false,\"error\":\"invalid_range\"}");
    return;
  }

  pulseMs = nextPulse;
  forceMs = nextForce;
  saveTimings();
  sendJson(200, "{\"ok\":true,\"message\":\"timings_updated\"}");
}

void handleApiSetWifi() {
  if (!guardJsonRateLimit(webSensitiveBucket, WEB_SENSITIVE_WINDOW_MS, WEB_SENSITIVE_MAX_HITS, WEB_SENSITIVE_MIN_GAP_MS, "rate_limited")) return;
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
  sendJson(200, "{\"ok\":true,\"message\":\"wifi_saved_reboot_recommended\"}");
}

void handleApiSetWifiMethodNotAllowed() {
  sendJson(405, "{\"ok\":false,\"error\":\"use_post\"}");
}

void handleApiForceShutdown() {
  if (!guardJsonRateLimit(webSensitiveBucket, WEB_SENSITIVE_WINDOW_MS, WEB_SENSITIVE_MAX_HITS, WEB_SENSITIVE_MIN_GAP_MS, "rate_limited")) return;
  if (requestPulseMs(forceMs, "http-force-shutdown"))
    sendJson(200, "{\"ok\":true,\"mode\":\"force_shutdown\",\"pulse_ms\":" + String(forceMs) + "}");
  else
    sendJson(429, "{\"ok\":false,\"error\":\"busy_or_lockout\"}");
}

void handleApiBootRecovery() {
  if (!guardJsonRateLimit(webSensitiveBucket, WEB_SENSITIVE_WINDOW_MS, WEB_SENSITIVE_MAX_HITS, WEB_SENSITIVE_MIN_GAP_MS, "rate_limited")) return;
  releasePcOutputForSensitiveAction("api-boot-recovery");
  logLine("[SYS] Boot recovery richiesto via API");
  if (!bootToFactory()) {
    sendJson(500, "{\"ok\":false,\"error\":\"factory_partition_not_found\"}");
    return;
  }
  rtcCrashCount = 0;
  rtcSoftLockRebootCount = 0;
  markIntentionalReboot();
  sendJson(200, "{\"ok\":true,\"message\":\"rebooting_to_factory\"}");
  delay(500);
  ESP.restart();
}

void handleApiReboot() {
  if (!guardJsonRateLimit(webSensitiveBucket, WEB_SENSITIVE_WINDOW_MS, WEB_SENSITIVE_MAX_HITS, WEB_SENSITIVE_MIN_GAP_MS, "rate_limited")) return;
  releasePcOutputForSensitiveAction("api-reboot");
  logLine("[SYS] Reboot richiesto via API");
  sendJson(200, "{\"ok\":true,\"message\":\"rebooting\"}");
  delay(150);
  markIntentionalReboot();
  ESP.restart();
}

void handleApiUpdateProgress() {
  sendJson(200, otaProgressJson());
}

void handleApiUpdateUpload() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    if (!consumeWebRateLimit(webOtaBucket, WEB_OTA_WINDOW_MS, WEB_OTA_MAX_HITS, WEB_OTA_MIN_GAP_MS)) {
      resetOtaUploadProgress();
      otaUploadFinished = true;
      otaUploadSuccess = false;
      otaUploadStatus = "rate_limited";
      return;
    }
    resetOtaUploadProgress();
    otaUploadInProgress = true;
    otaUploadStatus = "starting";
    otaUploadStartedMs = millis();
    otaLastChunkMs = millis();
    otaStallRecoveryRequested = false;
    otaTargetPartition = esp_ota_get_next_update_partition(nullptr);

    if (server.hasHeader("X-File-Size")) {
      otaUploadExpectedBytes = (uint32_t)strtoul(server.header("X-File-Size").c_str(), nullptr, 10);
    }
    if (otaTargetPartition == nullptr) {
      otaUploadInProgress = false;
      otaUploadFinished = true;
      otaUploadSuccess = false;
      otaUploadStatus = "target_partition_missing";
      appendWebLog("[OTA] Partizione OTA non trovata");
      return;
    }
    if (otaFilenameLooksUnsafe(upload.filename)) {
      otaUploadInProgress = false;
      otaUploadFinished = true;
      otaUploadSuccess = false;
      otaUploadStatus = "unsupported_image_name";
      appendWebLog("[OTA] Nome file non ammesso: " + upload.filename);
      return;
    }
    if (otaUploadExpectedBytes > 0 && otaUploadExpectedBytes > otaTargetPartition->size) {
      otaUploadInProgress = false;
      otaUploadFinished = true;
      otaUploadSuccess = false;
      otaUploadStatus = "image_too_large";
      appendWebLog("[OTA] Immagine troppo grande");
      return;
    }

    logLine("[OTA] Start: " + upload.filename);
    size_t otaBeginSize = otaUploadExpectedBytes > 0 ? otaUploadExpectedBytes : UPDATE_SIZE_UNKNOWN;
    if (!Update.begin(otaBeginSize, U_FLASH)) {
      otaUploadInProgress = false;
      otaUploadFinished = true;
      otaUploadSuccess = false;
      otaUploadStatus = "begin_failed";
      debugPrintUpdateError();
      appendWebLog("[OTA] Update.begin failed");
    } else {
      otaUploadStatus = "uploading";
    }
    yield();
    return;
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (!otaUploadInProgress || otaUploadFinished) {
      yield();
      return;
    }
    if (!validateOtaChunkHeader(upload.buf, upload.currentSize)) {
      otaUploadInProgress = false;
      otaUploadFinished = true;
      otaUploadSuccess = false;
      otaUploadStatus = "invalid_app_image";
      otaLastChunkMs = 0;
      Update.abort();
      appendWebLog("[OTA] Immagine non valida o non applicativa");
      yield();
      return;
    }
    size_t written = Update.write(upload.buf, upload.currentSize);
    otaUploadWrittenBytes += (uint32_t)written;
    otaLastChunkMs = millis();
    otaUploadStatus = "writing";
    if (written != upload.currentSize) {
      otaUploadInProgress = false;
      otaUploadFinished = true;
      otaUploadSuccess = false;
      otaUploadStatus = "write_failed";
      Update.abort();
      debugPrintUpdateError();
    }
    yield();
    return;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (otaUploadExpectedBytes == 0) otaUploadExpectedBytes = upload.totalSize;
    if (otaUploadExpectedBytes > 0 && otaUploadWrittenBytes < otaUploadExpectedBytes) {
      otaUploadInProgress = false;
      otaUploadFinished = true;
      otaUploadSuccess = false;
      otaUploadStatus = "incomplete_upload";
      otaLastChunkMs = 0;
      Update.abort();
      appendWebLog("[OTA] Upload incompleto");
    } else if (Update.end(false)) {
      String verifyStatus;
      if (verifyFlashedOtaPartition(otaTargetPartition, verifyStatus)) {
        otaUploadInProgress = false;
        otaUploadFinished = true;
        otaUploadSuccess = true;
        otaUploadStatus = "flash_ok";
        otaLastChunkMs = 0;
        logLine("[OTA] Success: " + String(upload.totalSize) + " bytes");
      } else {
        restoreRunningPartitionBootTarget();
        otaUploadInProgress = false;
        otaUploadFinished = true;
        otaUploadSuccess = false;
        otaUploadStatus = verifyStatus;
        otaLastChunkMs = 0;
        appendWebLog("[OTA] Verifica finale fallita: " + verifyStatus);
      }
    } else {
      otaUploadInProgress = false;
      otaUploadFinished = true;
      otaUploadSuccess = false;
      otaUploadStatus = "end_failed";
      otaLastChunkMs = 0;
      debugPrintUpdateError();
      appendWebLog("[OTA] Update.end failed");
    }
    yield();
    return;
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    otaUploadInProgress = false;
    otaUploadFinished = true;
    otaUploadSuccess = false;
    otaUploadStatus = "aborted";
    otaLastChunkMs = 0;
    Update.abort();
    logLine("[OTA] Aborted");
  }
}

void handleApiUpdate() {
  bool ok = otaUploadFinished && otaUploadSuccess && !Update.hasError();
  if (ok) {
    otaUploadInProgress = true;  // keep background traffic paused until reboot
    otaUploadStatus = "reboot_scheduled";
    otaLastChunkMs = 0;
    otaRebootScheduled = true;
    otaRebootAtMs = millis() + 2500;
    releasePcOutputForSensitiveAction("ota-reboot");
    logLine("[OTA] Update completo, reboot pianificato");
    sendJson(200, "{\"ok\":true,\"message\":\"update_complete_reboot_scheduled\"}");
  } else {
    otaUploadInProgress = false;
    otaUploadFinished = true;
    otaUploadSuccess = false;
    otaLastChunkMs = 0;
    if (otaUploadStatus == "idle") otaUploadStatus = "update_failed";
    appendWebLog("[OTA] Update fallito");
    sendJson(500, "{\"ok\":false,\"error\":\"" + jsonEscape(otaUploadStatus) + "\"}");
  }
}

void setupRoutes() {
  server.collectHeaders(OTA_UPLOAD_HEADERS, OTA_UPLOAD_HEADERS_COUNT);
  server.on("/", handleRoot);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/resetwifi", handleResetWiFi);
  server.on("/status", handleApiStatus);
  server.on("/api/status", handleApiStatus);
  server.on("/api/telegram/health", HTTP_GET, handleApiTelegramHealth);
  server.on("/api/telegram/recover", HTTP_GET, handleApiTelegramRecover);
  server.on("/api/logs", HTTP_GET, handleApiLogs);
  server.on("/api/logs/clear", HTTP_GET, handleApiLogsClear);
  server.on("/api/buglog", HTTP_GET, handleApiBugLogDump);
  server.on("/api/buglog/clear", HTTP_GET, handleApiBugLogClear);
  server.on("/api/boot-history", HTTP_GET, handleApiBootHistory);
  server.on("/api/wifi/scan", HTTP_GET, handleApiWifiScan);

  server.on("/api/pc/pulse", HTTP_GET, handleApiPulse);
  server.on("/api/pc/test/on", HTTP_GET, handleApiTestOn);
  server.on("/api/pc/test/off", HTTP_GET, handleApiTestOff);
  server.on("/api/config/timings", HTTP_GET, handleApiSetTimings);
  server.on("/api/config/wifi", HTTP_POST, handleApiSetWifi);
  server.on("/api/config/wifi", HTTP_GET, handleApiSetWifiMethodNotAllowed);
  server.on("/api/reboot", HTTP_GET, handleApiReboot);
  server.on("/api/boot-recovery", HTTP_GET, handleApiBootRecovery);
  server.on("/api/pc/forceshutdown", HTTP_GET, handleApiForceShutdown);
  server.on("/api/update/progress", HTTP_GET, handleApiUpdateProgress);
  server.on("/api/update", HTTP_POST, handleApiUpdate, handleApiUpdateUpload);

  server.onNotFound([]() { sendText(404, "Not found"); });
}

void setupPins() {
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(PC_PWR_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
  setPcOutput(false);
}

void setup() {
  Serial.begin(115200);
  bootMillisBaseline = millis();
  buglog_begin();
  clearWebLogs();
  resetOtaUploadProgress();
  bootSessionId = esp_random();
  if (bootSessionId == 0) bootSessionId = 1;
  bootResetReason = esp_reset_reason();
  bool intentionalSwReset = (rtcIntentionalReboot != 0);
  rtcIntentionalReboot = 0;
  logLine("[SYS] Boot");
  log_event(BUGLOG_INFO, 1000, "boot");
  logLine("[SYS] Boot session: " + bootSessionIdHex());
  logLine("[SYS] Reset reason: " + String((int)bootResetReason));
  if (!TELEGRAM_RUNTIME_ENABLED) {
    logLine("[TG] Diagnostic build: Telegram disabilitato");
  }
  if (bootResetReason == ESP_RST_SW) {
    logLine(String("[SYS] SW reset ") + (intentionalSwReset ? "intenzionale" : "non marcato"));
  }

  // Crash counter: incrementa su reset anomali, attiva safe mode se >= soglia
  bool isCrashReset = (bootResetReason == ESP_RST_PANIC ||
                       bootResetReason == ESP_RST_INT_WDT ||
                       bootResetReason == ESP_RST_TASK_WDT ||
                       bootResetReason == ESP_RST_WDT);
  if (isCrashReset) {
    if (rtcCrashCount < 255) rtcCrashCount++;  // BUG-3 FIX: guard overflow uint8_t
    logLine("[SYS] Crash rilevato, contatore: " + String(rtcCrashCount));
    log_event(BUGLOG_ERROR, 1001, "crash_reset");
  } else {
    // Reset normale (power-on, SW, OTA) non incrementa ??? ma non azzera neanche subito:
    // il contatore si azzera solo dopo 60s di uptime stabile (vedi loop)
  }
  if (rtcCrashCount >= FACTORY_BOOT_CRASH_THRESHOLD) {
    logLine("[SYS] Troppi crash (" + String(rtcCrashCount) + "), salto alla factory partition...");
    rtcCrashCount = 0;
    if (bootToFactory()) {
      rtcSoftLockRebootCount = 0;
      markIntentionalReboot();
      delay(200);
      ESP.restart();
    } else {
      logLine("[SYS] Factory partition non trovata, continuo in safe mode");
    }
  }

  if (bootResetReason == ESP_RST_SW && !intentionalSwReset) {
    if (rtcSoftLockRebootCount >= SOFTLOCK_FACTORY_BOOT_THRESHOLD) {
      logLine("[SYS] Troppi reboot soft-lock (" + String(rtcSoftLockRebootCount) + "), salto alla factory partition...");
      if (bootToFactory()) {
        rtcSoftLockRebootCount = 0;
        rtcCrashCount = 0;
        markIntentionalReboot();
        delay(200);
        ESP.restart();
      } else {
        logLine("[SYS] Factory partition non trovata dopo soft-lock, continuo");
      }
    }
  }
  if (rtcCrashCount >= SAFE_MODE_CRASH_THRESHOLD) {
    safeModeActive = true;
    logLine("[SYS] SAFE MODE attiva (crash=" + String(rtcCrashCount) + ")");
  }

  if (telegramHttpMutex == nullptr) {
    telegramHttpMutex = xSemaphoreCreateMutex();
  }
  if (logMutex == nullptr) {
    logMutex = xSemaphoreCreateMutex();
  }
  {
    ScopedHeapProbe heapProbe("bt_cfg", 1610, heapProbeBootConfig);
    if (deviceCmdQueue == nullptr) {
      deviceCmdQueue = xQueueCreate(8, sizeof(DeviceCmd));
    }
    if (prefsMutex == nullptr) {
      prefsMutex = xSemaphoreCreateMutex();
    }
    appendPersistentBootEvent(
      "boot reset=" + String((int)bootResetReason) +
      " intent=" + String(intentionalSwReset ? 1 : 0) +
      " crash=" + String(rtcCrashCount) +
      " soft=" + String(rtcSoftLockRebootCount)
    );
    setupPins();
    loadTimings();
    loadWifiConfig();
    loadTelegramState();
    if (wifiSsidConfig.length() == 0 && wm.getWiFiIsSaved()) {
      syncWifiConfigFromSystem("preload");
    }
  }
  logBootStageSnapshot("bt_cfg", 1620);

  {
    ScopedHeapProbe heapProbe("bt_wifi", 1611, heapProbeBootWifi);
    // WiFi stability
    WiFi.setSleep(false);
    WiFi.persistent(true);
    WiFi.setAutoReconnect(true);
    WiFi.mode(WIFI_STA);

    // WiFiManager
    wm.setConfigPortalBlocking(true);
    wm.setConfigPortalTimeout(180);                 // evita blocchi indefiniti in setup
    wm.setConnectTimeout(WIFI_CONNECT_TIMEOUT_SEC); // ? 30 secondi
    wm.setSaveConfigCallback([]() { wifiConfigNeedsSync = true; });
    bool hasStoredWifi = havePersistentWifiConfig();
    bool connected = connectConfiguredWifi((unsigned long)WIFI_CONNECT_TIMEOUT_SEC * 1000UL);
    if (!connected) {
      if (!hasStoredWifi) {
        bool portalConnected = wm.autoConnect("ESP32S2-Setup");
        if (!portalConnected) {
          logLine("[WiFi] Config portal timeout/uscita, AP fallback attivato");
          startFallbackAp();
        }
      } else {
        logLine("[WiFi] Configurato ma non raggiungibile: attivo AP fallback e continuo retry STA");
        startFallbackAp();
      }
    }
  }
  logBootStageSnapshot("bt_wifi", 1621);

  if (wifiConnected()) {
    if (wifiConfigNeedsSync || wifiSsidConfig.length() == 0) {
      syncWifiConfigFromSystem("boot");
      wifiConfigNeedsSync = false;
    }
    logLine("[WiFi] Connesso");
    logLine("[WiFi] IP: " + WiFi.localIP().toString());
    {
      ScopedHeapProbe heapProbe("bt_ntp", 1612, heapProbeBootNtp);
      if (ensureTimeSynced(1500)) logLine("[NTP] Ora sincronizzata");
      else logLine("[NTP] Sync timeout");
    }
    logBootStageSnapshot("bt_ntp", 1622);
    scheduleNodeRedPresenceReport("boot", NODERED_REPORT_BOOT_DELAY_MS);
    if (telegramConfigured()) scheduleTelegramWebhookRecovery("startup_boot", 2000);
    if (TELEGRAM_BOOT_NOTIFY_ENABLED) scheduleTelegramNotification("boot", 15000);  // 15s: lascia tempo all'heap di stabilizzarsi dopo WiFi+NTP+HTTP
  } else {
    logLine("[WiFi] Non connesso (AP mode attivo).");
  }

  lastWiFiConnected = wifiConnected();
  wifiOfflineSinceMs = lastWiFiConnected ? 0 : millis();

  {
    ScopedHeapProbe heapProbe("bt_http", 1613, heapProbeBootHttp);
    setupRoutes();
    server.begin();
    logLine("[HTTP] Server avviato");
  }
  logBootStageSnapshot("bt_http", 1623);

  {
    ScopedHeapProbe heapProbe("bt_task", 1614, heapProbeBootTasks);
    if (!safeModeActive && TELEGRAM_TASK_AUTOSTART && telegramTaskHandle == nullptr) {
      startTelegramTask("boot");
    } else if (safeModeActive) {
      logLine("[SYS] SAFE MODE: telegramTask non avviato. Usa la web UI per OTA.");
    }

    if (safetyTaskHandle == nullptr) {
      BaseType_t safeCreateOk = xTaskCreate(
        safetyTask,
        "safetyTask",
        6144,  // BUG-7 FIX: 4096 era tight; restartFromWatchdog chiama logLine+buglog_flush(8)+NVS write
        nullptr,
        1,
        &safetyTaskHandle
      );
      if (safeCreateOk != pdPASS || safetyTaskHandle == nullptr) {
        logLine("[SYS] safetyTask create FAILED");
        log_event(BUGLOG_ERROR, 1302, "safety_task_create_failed");
        appendPersistentBootEvent("task_create_failed name=safetyTask");
      }
    }
  }
  logBootStageSnapshot("bt_task", 1624);

  markLoopHeartbeat();
  markTelegramHeartbeat();
}

void loop() {
  markLoopHeartbeat();
  buglog_tick();
  maybeLogPersistentHeartbeat();
  maybeRecoverTelegramTask();
  processNodeRedPresenceReport();
  processDeviceCmdQueue();

  // Dopo SAFE_MODE_CLEAR_MS di uptime stabile, azzera contatore crash
  if (rtcCrashCount > 0 && safeModeClearAtMs == 0 && wifiConnected()) {
    safeModeClearAtMs = millis() + SAFE_MODE_CLEAR_MS;
  }
  if (safeModeClearAtMs > 0 && (long)(millis() - safeModeClearAtMs) >= 0) {
    safeModeClearAtMs = 0;
    if (rtcCrashCount > 0) {
      logLine("[SYS] Uptime stabile, crash counter azzerato (era " + String(rtcCrashCount) + ")");
      rtcCrashCount = 0;
      safeModeActive = false;
    }
  }

  if (rtcSoftLockRebootCount > 0 && softLockClearAtMs == 0 && wifiConnected()) {
    softLockClearAtMs = millis() + SOFTLOCK_CLEAR_MS;
  }
  if (softLockClearAtMs > 0 && (long)(millis() - softLockClearAtMs) >= 0) {
    softLockClearAtMs = 0;
    if (rtcSoftLockRebootCount > 0 && wifiConnected() && !otaUploadInProgress) {
      logLine("[SYS] Uptime stabile, soft-lock reboot counter azzerato (era " + String(rtcSoftLockRebootCount) + ")");
      rtcSoftLockRebootCount = 0;
    }
  }

  if (otaStallRecoveryRequested) {
    otaStallRecoveryRequested = false;
    otaUploadInProgress = false;
    otaUploadFinished = true;
    otaUploadSuccess = false;
    otaUploadStatus = "stalled_reboot";
    otaRebootScheduled = true;
    otaRebootAtMs = millis() + 1500;
    otaLastChunkMs = 0;
    logLine("[OTA] Stall rilevato: reboot di recovery");
  }

  if (otaRebootScheduled && (long)(millis() - otaRebootAtMs) >= 0) {
    logLine("[OTA] Riavvio in corso");
    delay(100);
    markIntentionalReboot();
    ESP.restart();
    return;
  }

  server.handleClient();
  updatePulseState();
  processTestModeTimeout();
  updateStatusLed();

  bool wifiNowConnected = wifiConnected();
  if (wifiNowConnected != lastWiFiConnected) {
    lastWiFiConnected = wifiNowConnected;
    if (wifiNowConnected) {
      wifiOfflineSinceMs = 0;
      stopFallbackAp();
      if (ensureTimeSynced(1500)) logLine("[NTP] Ora sincronizzata");
      else logLine("[NTP] Sync timeout");
      scheduleNodeRedPresenceReport("wifi_reconnected", 500);
      if (telegramConfigured()) scheduleTelegramWebhookRecovery("wifi_reconnected", 800);
      if (TELEGRAM_BOOT_NOTIFY_ENABLED) scheduleTelegramNotification("wifi_reconnected", 1000);
      logLine("[WiFi] Riconnesso");
    } else {
      if (wifiOfflineSinceMs == 0) wifiOfflineSinceMs = millis();
      logLine("[WiFi] Disconnesso");
    }
  }

  if (!wifiNowConnected && wifiOfflineSinceMs > 0) {
    if (!fallbackApActive && (long)(millis() - wifiOfflineSinceMs) >= (long)WIFI_OFFLINE_TO_AP_MS) {
      startFallbackAp();
    }
  }

  // retry soft se cade
  static unsigned long nextWiFiTry = 0;
  if (!wifiConnected() && (nextWiFiTry == 0 || (long)(millis() - nextWiFiTry) >= 0)) {
    nextWiFiTry = millis() + WIFI_RECONNECT_EVERY_MS;
    WiFi.reconnect();
  }

  if (wifiNowConnected && (wifiConfigNeedsSync || wifiSsidConfig.length() == 0)) {
    syncWifiConfigFromSystem("runtime");
    wifiConfigNeedsSync = false;
  }
}














