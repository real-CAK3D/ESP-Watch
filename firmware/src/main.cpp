// GTA-Watch: GTA V style minimap + turn-by-turn navigation smartwatch firmware.
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_ota_ops.h>
#include <lvgl.h>

#include "console.h"
#include "hal.h"
#include "mapdata.h"
#include "ota.h"
#include "terrain.h"
#include "phone.h"
#include "state.h"
#include "ui.h"

void setup() {
  Serial.setRxBufferSize(16384);
  Serial.setTxBufferSize(16384);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(250);  // wait for the host instead of silently dropping output
  setenv("TZ", "UTC0", 1);
  tzset();

  if (!LittleFS.begin(true)) Serial.println("[main] LittleFS mount failed");
  hal::begin();
  hal::lvglBegin();
  state::begin();
  mapdata::restore();
  terrain::restore();
  ui::begin();
  console::begin();
  phone::begin();
  esp_ota_mark_app_valid_cancel_rollback();  // booted fine after an OTA update
  Serial.printf("[main] GTA-Watch %s ready, heap %u, psram %u\n", FW_VERSION,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

void loop() {
  hal::loop();
  phone::loop();
  console::loop();
  ota::loop();
  ui::loop();
  uint32_t wait = lv_timer_handler();
  delay(hal::screenIsOn() ? min<uint32_t>(wait, 5) : 40);
}
