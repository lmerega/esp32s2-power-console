#pragma once

#include <Arduino.h>

// Call from setup/loop only. The WiFi callback only copies events to a queue.
void wifiDiagnosticsBegin();
void wifiDiagnosticsPoll();
void wifiDiagnosticsReconnectAttempt(bool accepted);
void wifiDiagnosticsScanStart();
void wifiDiagnosticsScanEnd(const char *outcome, int result);
void wifiDiagnosticsScanResponse(const char *phase, uint32_t detail);
void wifiDiagnosticsScanRejected();
void wifiDiagnosticsAppendJson(String &json);
