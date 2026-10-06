// BLE link to the phone app (NimBLE peripheral) plus the shared inbound message queue.
#pragma once
#include <Arduino.h>

namespace phone {
void begin();
void loop();  // drains inbound messages into state::handleMessage, sends periodic telemetry
bool connected();
int mtu();

// Queue a complete message for the main loop (used by BLE and the USB debug console).
void enqueue(uint8_t type, const uint8_t *data, size_t len);

void send(uint8_t type, const uint8_t *data, size_t len);
void sendJson(uint8_t type, const char *json);
void sendEvent(const char *name, int index = -1);
void sendTelemetry();
}  // namespace phone
