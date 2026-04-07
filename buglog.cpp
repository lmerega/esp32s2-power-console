#include "buglog.h"

#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

constexpr uint16_t kEntryMagic = 0xB17E;
constexpr uint8_t kEntryVersion = 1;
constexpr uint16_t kRingCapacity = 48;
constexpr size_t kMessageBytes = 64;
constexpr size_t kQueueCapacity = 8;
constexpr unsigned long kFlushIntervalMs = 40;

struct __attribute__((packed)) StoredEntry {
  uint16_t magic;
  uint8_t version;
  uint8_t severity;
  uint16_t code;
  uint32_t uptimeMs;
  char message[kMessageBytes];
  uint8_t crc8;
};

struct PendingQueue {
  StoredEntry items[kQueueCapacity];
  uint8_t head = 0;
  uint8_t tail = 0;
  uint8_t count = 0;
};

Preferences gPrefs;
SemaphoreHandle_t gStorageMutex = nullptr;
portMUX_TYPE gQueueMux = portMUX_INITIALIZER_UNLOCKED;
PendingQueue gQueue;
bool gReady = false;
bool gMetaLoaded = false;
uint16_t gHead = 0;
uint16_t gCount = 0;
unsigned long gNextFlushMs = 0;

uint8_t crc8_update(uint8_t crc, uint8_t data) {
  crc ^= data;
  for (uint8_t bit = 0; bit < 8; ++bit) {
    crc = (crc & 0x80U) ? static_cast<uint8_t>((crc << 1U) ^ 0x07U) : static_cast<uint8_t>(crc << 1U);
  }
  return crc;
}

uint8_t crc8_buffer(const uint8_t *data, size_t len) {
  uint8_t crc = 0x00;
  for (size_t i = 0; i < len; ++i) {
    crc = crc8_update(crc, data[i]);
  }
  return crc;
}

bool lockStorage(TickType_t timeout = pdMS_TO_TICKS(40)) {
  if (gStorageMutex == nullptr) return false;
  return xSemaphoreTake(gStorageMutex, timeout) == pdTRUE;
}

void unlockStorage() {
  if (gStorageMutex != nullptr) {
    xSemaphoreGive(gStorageMutex);
  }
}

void copyMessage(char *dest, size_t destSize, const char *src) {
  if (destSize == 0) return;
  if (src == nullptr) {
    dest[0] = '\0';
    return;
  }
  size_t i = 0;
  for (; i + 1 < destSize && src[i] != '\0'; ++i) {
    dest[i] = src[i];
  }
  dest[i] = '\0';
}

void fillEntry(StoredEntry &entry, BugSeverity severity, uint16_t code, const char *msg) {
  memset(&entry, 0, sizeof(entry));
  entry.magic = kEntryMagic;
  entry.version = kEntryVersion;
  entry.severity = static_cast<uint8_t>(severity);
  entry.code = code;
  entry.uptimeMs = millis();
  copyMessage(entry.message, sizeof(entry.message), msg);
  entry.crc8 = crc8_buffer(reinterpret_cast<const uint8_t *>(&entry), sizeof(entry) - 1);
}

bool entryValid(const StoredEntry &entry) {
  if (entry.magic != kEntryMagic || entry.version != kEntryVersion) return false;
  const uint8_t expected = crc8_buffer(reinterpret_cast<const uint8_t *>(&entry), sizeof(entry) - 1);
  return expected == entry.crc8;
}

void slotKey(uint16_t slot, char *out, size_t outSize) {
  // Gap-2 FIX: %03u invece di %02u — futuro-safe se kRingCapacity supera 99
  // Chiave NVS max 15 char; "e" + 3 cifre = 4 char, abbondantemente ok
  snprintf(out, outSize, "e%03u", static_cast<unsigned>(slot % kRingCapacity));
}

bool openPrefs(bool readOnly) {
  return gPrefs.begin("buglog", readOnly);
}

bool resetMetadataLocked() {
  if (!openPrefs(false)) return false;
  gPrefs.clear();
  gHead = 0;
  gCount = 0;
  gMetaLoaded = true;
  bool ok = true;
  ok &= gPrefs.putUShort("head", gHead) > 0;
  ok &= gPrefs.putUShort("count", gCount) > 0;
  ok &= gPrefs.putUInt("magic", 0x424C4F47UL) > 0;
  ok &= gPrefs.putUChar("ver", kEntryVersion) > 0;
  gPrefs.end();
  return ok;
}

bool loadMetadataLocked() {
  if (!openPrefs(false)) return false;
  const uint32_t magic = gPrefs.getUInt("magic", 0);
  const uint8_t version = gPrefs.getUChar("ver", 0);
  if (magic != 0x424C4F47UL || version != kEntryVersion) {
    gPrefs.end();
    return resetMetadataLocked();
  }
  gHead = gPrefs.getUShort("head", 0);
  gCount = gPrefs.getUShort("count", 0);
  if (gHead >= kRingCapacity) gHead = 0;
  if (gCount > kRingCapacity) gCount = 0;
  gMetaLoaded = true;
  gPrefs.end();
  return true;
}

bool persistEntryLocked(const StoredEntry &entry) {
  if (!gMetaLoaded && !loadMetadataLocked()) return false;
  if (!openPrefs(false)) return false;

  char key[8];
  slotKey(gHead, key, sizeof(key));
  const size_t written = gPrefs.putBytes(key, &entry, sizeof(entry));
  if (written != sizeof(entry)) {
    gPrefs.end();
    return false;
  }

  gHead = static_cast<uint16_t>((gHead + 1U) % kRingCapacity);
  if (gCount < kRingCapacity) {
    ++gCount;
  }

  bool ok = true;
  ok &= gPrefs.putUShort("head", gHead) > 0;
  ok &= gPrefs.putUShort("count", gCount) > 0;
  gPrefs.end();
  return ok;
}

bool dequeueEntry(StoredEntry &entry) {
  bool haveItem = false;
  portENTER_CRITICAL(&gQueueMux);
  if (gQueue.count > 0) {
    entry = gQueue.items[gQueue.tail];
    gQueue.tail = static_cast<uint8_t>((gQueue.tail + 1U) % kQueueCapacity);
    --gQueue.count;
    haveItem = true;
  }
  portEXIT_CRITICAL(&gQueueMux);
  return haveItem;
}

bool enqueueEntry(const StoredEntry &entry) {
  bool enqueued = false;
  portENTER_CRITICAL(&gQueueMux);
  if (gQueue.count < kQueueCapacity) {
    gQueue.items[gQueue.head] = entry;
    gQueue.head = static_cast<uint8_t>((gQueue.head + 1U) % kQueueCapacity);
    ++gQueue.count;
    enqueued = true;
  }
  portEXIT_CRITICAL(&gQueueMux);
  return enqueued;
}

const char *severityName(BugSeverity severity) {
  switch (severity) {
    case BUGLOG_INFO: return "INFO";
    case BUGLOG_WARN: return "WARN";
    case BUGLOG_ERROR: return "ERROR";
    case BUGLOG_FATAL: return "FATAL";
    default: return "UNKNOWN";
  }
}

}  // namespace

bool buglog_begin() {
  if (gReady) return true;
  if (gStorageMutex == nullptr) {
    gStorageMutex = xSemaphoreCreateMutex();
    if (gStorageMutex == nullptr) return false;
  }
  if (!lockStorage(pdMS_TO_TICKS(120))) return false;
  const bool ok = loadMetadataLocked();
  unlockStorage();
  gReady = ok;
  return ok;
}

void buglog_tick() {
  if (!gReady) return;
  const unsigned long now = millis();
  if (gNextFlushMs != 0 && static_cast<long>(now - gNextFlushMs) < 0) return;
  if (buglog_flush(1)) {
    gNextFlushMs = millis() + kFlushIntervalMs;
  } else if (gNextFlushMs == 0) {
    gNextFlushMs = millis() + kFlushIntervalMs;
  }
}

bool buglog_flush(size_t maxEntries) {
  if (!gReady || maxEntries == 0) return false;
  if (!lockStorage()) return false;

  bool wrote = false;
  size_t flushed = 0;
  StoredEntry entry;
  while (flushed < maxEntries && dequeueEntry(entry)) {
    if (!persistEntryLocked(entry)) break;
    wrote = true;
    ++flushed;
  }

  unlockStorage();
  return wrote;
}

bool log_event(BugSeverity severity, uint16_t code, const char *msg) {
  if (!gReady && !buglog_begin()) return false;
  StoredEntry entry;
  fillEntry(entry, severity, code, msg);
  return enqueueEntry(entry);
}

void log_dump(Stream &out) {
  if (!gReady && !buglog_begin()) {
    out.println(F("BUGLOG unavailable"));
    return;
  }
  if (!lockStorage(pdMS_TO_TICKS(120))) {
    out.println(F("BUGLOG busy"));
    return;
  }
  if (!gMetaLoaded && !loadMetadataLocked()) {
    unlockStorage();
    out.println(F("BUGLOG metadata error"));
    return;
  }
  if (!openPrefs(true)) {
    unlockStorage();
    out.println(F("BUGLOG open failed"));
    return;
  }

  out.print(F("# buglog count="));
  out.println(gCount);
  for (uint16_t i = 0; i < gCount; ++i) {
    const uint16_t oldest = static_cast<uint16_t>((gHead + kRingCapacity - gCount + i) % kRingCapacity);
    char key[8];
    slotKey(oldest, key, sizeof(key));

    StoredEntry entry{};
    const size_t read = gPrefs.getBytes(key, &entry, sizeof(entry));
    out.print(i);
    out.print(F(": "));
    if (read != sizeof(entry) || !entryValid(entry)) {
      out.println(F("CORRUPT"));
      continue;
    }

    out.print(F("uptime_ms="));
    out.print(entry.uptimeMs);
    out.print(F(" sev="));
    out.print(severityName(static_cast<BugSeverity>(entry.severity)));
    out.print(F(" code="));
    out.print(entry.code);
    if (entry.message[0] != '\0') {
      out.print(F(" msg="));
      out.print(entry.message);
    }
    out.println();
  }

  gPrefs.end();
  unlockStorage();
}

String buglog_dump_string() {
  String out;
  out.reserve(1024);

  if (!gReady && !buglog_begin()) {
    out = F("BUGLOG unavailable\n");
    return out;
  }
  if (!lockStorage(pdMS_TO_TICKS(120))) {
    out = F("BUGLOG busy\n");
    return out;
  }
  if (!gMetaLoaded && !loadMetadataLocked()) {
    unlockStorage();
    out = F("BUGLOG metadata error\n");
    return out;
  }
  if (!openPrefs(true)) {
    unlockStorage();
    out = F("BUGLOG open failed\n");
    return out;
  }

  out += F("# buglog count=");
  out += String(gCount);
  out += '\n';

  for (uint16_t i = 0; i < gCount; ++i) {
    const uint16_t oldest = static_cast<uint16_t>((gHead + kRingCapacity - gCount + i) % kRingCapacity);
    char key[8];
    slotKey(oldest, key, sizeof(key));

    StoredEntry entry{};
    const size_t read = gPrefs.getBytes(key, &entry, sizeof(entry));
    out += String(i);
    out += F(": ");
    if (read != sizeof(entry) || !entryValid(entry)) {
      out += F("CORRUPT\n");
      continue;
    }

    out += F("uptime_ms=");
    out += String(entry.uptimeMs);
    out += F(" sev=");
    out += severityName(static_cast<BugSeverity>(entry.severity));
    out += F(" code=");
    out += String(entry.code);
    if (entry.message[0] != '\0') {
      out += F(" msg=");
      out += entry.message;
    }
    out += '\n';
  }

  gPrefs.end();
  unlockStorage();
  return out;
}

bool log_clear() {
  if (!gReady && !buglog_begin()) return false;
  portENTER_CRITICAL(&gQueueMux);
  gQueue.head = 0;
  gQueue.tail = 0;
  gQueue.count = 0;
  portEXIT_CRITICAL(&gQueueMux);
  if (!lockStorage(pdMS_TO_TICKS(120))) return false;
  const bool ok = resetMetadataLocked();
  unlockStorage();
  return ok;
}

const char *buglog_severity_name(BugSeverity severity) {
  return severityName(severity);
}
