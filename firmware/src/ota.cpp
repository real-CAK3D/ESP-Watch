#include "ota.h"

#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <lvgl.h>

#include "hal.h"
#include "phone.h"
#include "proto.h"
#include "ui.h"

static esp_ota_handle_t otaHandle = 0;
static const esp_partition_t *target = nullptr;
static size_t total = 0, written = 0;
static bool running = false;
static uint32_t lastRxMs = 0;

static void status(const char *st, const char *err = nullptr) {
  char buf[128];
  if (err) snprintf(buf, sizeof(buf), "{\"st\":\"%s\",\"off\":%u,\"err\":\"%s\"}", st, (unsigned)written, err);
  else snprintf(buf, sizeof(buf), "{\"st\":\"%s\",\"off\":%u}", st, (unsigned)written);
  phone::sendJson(MSG_OTA_STATUS, buf);
}

static void fail(const char *why) {
  if (running && otaHandle) esp_ota_abort(otaHandle);
  running = false;
  otaHandle = 0;
  status("error", why);
  ui::otaProgress(-1, why);
}

namespace ota {

bool active() { return running; }
int progress() { return total ? (int)(written * 100 / total) : 0; }

void handle(uint8_t type, const uint8_t *data, size_t len) {
  lastRxMs = millis();
  if (type == MSG_OTA_BEGIN) {
    JsonDocument doc;
    if (deserializeJson(doc, (const char *)data, len)) return fail("bad begin");
    if (running && otaHandle) esp_ota_abort(otaHandle);
    total = doc["size"] | 0;
    written = 0;
    target = esp_ota_get_next_update_partition(nullptr);
    if (!target || total == 0 || total > target->size) return fail("image too big");
    hal::screenOn(true);
    ui::otaProgress(0, doc["ver"] | "");
    lv_timer_handler();  // show the update screen before the (blocking) erase
    // erases only the sectors the image needs (~1-2 s per MB)
    esp_err_t e = esp_ota_begin(target, total, &otaHandle);
    if (e != ESP_OK) return fail(esp_err_to_name(e));
    running = true;
    status("ready");
    return;
  }
  if (!running) return fail("not started");
  if (type == MSG_OTA_DATA) {
    if (len < 4) return;
    uint32_t off = data[0] | (data[1] << 8) | (data[2] << 16) | ((uint32_t)data[3] << 24);
    if (off == written) {  // anything else is a resend of an older block: just re-ACK where we are
      esp_err_t e = esp_ota_write(otaHandle, data + 4, len - 4);
      if (e != ESP_OK) return fail(esp_err_to_name(e));
      written += len - 4;
      hal::noteActivity();
      ui::otaProgress(progress(), nullptr);
    }
    status("ack");
    return;
  }
  if (type == MSG_OTA_END) {
    if (written != total) return fail("incomplete");
    esp_err_t e = esp_ota_end(otaHandle);  // verifies the image checksum
    otaHandle = 0;
    running = false;
    if (e != ESP_OK) return fail(esp_err_to_name(e));
    e = esp_ota_set_boot_partition(target);
    if (e != ESP_OK) return fail(esp_err_to_name(e));
    status("done");
    ui::otaProgress(100, "Restarting");
    uint32_t t0 = millis();
    while (millis() - t0 < 1500) {  // let the notification and the last frame go out
      lv_timer_handler();
      delay(10);
    }
    esp_restart();
  }
}

void loop() {
  if (running && millis() - lastRxMs > 30000) fail("timed out");
}

}  // namespace ota
