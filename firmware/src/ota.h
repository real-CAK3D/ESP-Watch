// Firmware update over BLE from the phone app (writes the spare OTA slot, then reboots into it).
#pragma once
#include <Arduino.h>

namespace ota {
void handle(uint8_t type, const uint8_t *data, size_t len);
void loop();       // aborts a stalled update
bool active();
int progress();    // 0..100
}  // namespace ota
