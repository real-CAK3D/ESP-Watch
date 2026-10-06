// USB serial debug console. Lets the PC tools (tools/watchctl.py) see and drive the watch:
//   shot                      -> "SHOT <w> <h> <bytes> <lines>" + "L <i> <adler> <b64>" lines + "END"
//   shotline <i>              -> resend one screenshot line
//   tap <x> <y>               -> synthetic tap
//   hold <x> <y>              -> synthetic long press
//   swipe <x0> <y0> <x1> <y1> -> synthetic swipe
//   m <type> <seq> <total> <adler> <b64>  -> one chunk of a phone message (ACKed "OK m <seq>")
//   msg <type-hex> <base64>   -> small phone message in one line (no checksum)
//   page <name>               -> weather|face|map|places|activity|quick|notifications
//   wake | sleep | info
#include "console.h"

#include <lvgl.h>
#include <mbedtls/base64.h>

#include "hal.h"
#include "mapdata.h"
#include "phone.h"
#include "state.h"
#include "ui.h"

namespace {

constexpr size_t kLineMax = 400 * 1024;
char *line = nullptr;
size_t lineLen = 0;

struct Step {
  int x, y;
  bool pressed;
  uint32_t at;
};
Step steps[48];
int nSteps = 0, stepIdx = 0;

void schedule(int x, int y, bool pressed, uint32_t delayMs) {
  if (nSteps >= 48) return;
  uint32_t base = nSteps ? steps[nSteps - 1].at : millis();
  steps[nSteps++] = {x, y, pressed, base + delayMs};
}

// The ESP32-S3 USB-JTAG serial port silently drops bytes when its buffers fill, so every
// bulk transfer is chunked and checksummed (Adler-32): the host re-requests bad screenshot
// lines, and the watch ACKs each inbound message chunk.
uint32_t adler32(const uint8_t *p, size_t n) {
  uint32_t a = 1, b = 0;
  while (n--) {
    a = (a + *p++) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}

void writeAll(const char *s, size_t n) {
  for (size_t sent = 0; sent < n;) {
    size_t w = Serial.write((const uint8_t *)s + sent, n - sent);
    if (w == 0) delay(2);
    sent += w;
  }
}

// Screenshot: run-length encoded RGB565 ([count u8][pixel u16le] records).
constexpr size_t SHOT_LINE_RAW = 1536;
uint8_t *shotBuf = nullptr;
size_t shotLen = 0;

void sendShotLine(size_t i) {
  size_t off = i * SHOT_LINE_RAW;
  if (off >= shotLen) return;
  size_t len = min(SHOT_LINE_RAW, shotLen - off), olen = 0;
  char out[2200];
  int h = snprintf(out, sizeof(out), "L %u %08lx ", (unsigned)i, (unsigned long)adler32(shotBuf + off, len));
  mbedtls_base64_encode((unsigned char *)out + h, sizeof(out) - h, &olen, shotBuf + off, len);
  olen += h;
  out[olen++] = '\n';
  writeAll(out, olen);
}

void sendShot() {
  uint16_t *fb = hal::shadowFramebuffer();
  if (!fb) return;
  const size_t npx = hal::W * hal::H;
  if (!shotBuf) shotBuf = (uint8_t *)heap_caps_malloc(npx * 3, MALLOC_CAP_SPIRAM);
  if (!shotBuf) return;
  size_t n = 0;
  for (size_t i = 0; i < npx;) {
    uint16_t v = fb[i];
    size_t run = 1;
    while (i + run < npx && run < 255 && fb[i + run] == v) run++;
    shotBuf[n++] = (uint8_t)run;
    shotBuf[n++] = v & 0xFF;
    shotBuf[n++] = v >> 8;
    i += run;
  }
  shotLen = n;
  size_t lines = (n + SHOT_LINE_RAW - 1) / SHOT_LINE_RAW;
  Serial.printf("SHOT %d %d %u %u\n", hal::W, hal::H, (unsigned)n, (unsigned)lines);
  for (size_t i = 0; i < lines; i++) sendShotLine(i);
  Serial.print("END\n");
}

// Inbound chunked message: "m <type-hex> <seq> <total> <adler-hex> <base64>"
uint8_t *msgBuf = nullptr;
size_t msgLen = 0;
int msgNext = 0;
uint8_t msgType = 0;
constexpr size_t MSG_MAX = 256 * 1024;

void handleChunk(char *arg) {
  unsigned type = 0, seq = 0, total = 0;
  unsigned long sum = 0;
  int consumed = 0;
  if (sscanf(arg, "%x %u %u %lx %n", &type, &seq, &total, &sum, &consumed) < 4) {
    Serial.println("ERR m parse");
    return;
  }
  const char *b64 = arg + consumed;
  if (!msgBuf) msgBuf = (uint8_t *)heap_caps_malloc(MSG_MAX, MALLOC_CAP_SPIRAM);
  if (seq == 0) {
    msgLen = 0;
    msgNext = 0;
    msgType = type;
  }
  if ((int)seq < msgNext) {  // duplicate (our ACK was lost)
    Serial.printf("OK m %u\n", seq);
    return;
  }
  if ((int)seq != msgNext || type != msgType) {
    Serial.printf("ERR m %u order\n", seq);
    return;
  }
  size_t inLen = strlen(b64), outLen = 0;
  if (msgLen + inLen > MSG_MAX ||
      mbedtls_base64_decode(msgBuf + msgLen, MSG_MAX - msgLen, &outLen, (const uint8_t *)b64, inLen) != 0 ||
      adler32(msgBuf + msgLen, outLen) != sum) {
    Serial.printf("ERR m %u crc\n", seq);
    return;
  }
  msgLen += outLen;
  msgNext++;
  if (seq + 1 == total) phone::enqueue(msgType, msgBuf, msgLen);
  Serial.printf("OK m %u\n", seq);
}

void handle(char *cmd) {
  char *arg = strchr(cmd, ' ');
  if (arg) *arg++ = 0;
  if (!strcmp(cmd, "shot")) {
    sendShot();
  } else if (!strcmp(cmd, "shotline") && arg) {
    sendShotLine(strtoul(arg, nullptr, 10));
  } else if (!strcmp(cmd, "m") && arg) {
    handleChunk(arg);
  } else if (!strcmp(cmd, "tap") || !strcmp(cmd, "hold")) {
    int x = 0, y = 0;
    sscanf(arg ? arg : "", "%d %d", &x, &y);
    nSteps = stepIdx = 0;
    schedule(x, y, true, 0);
    schedule(x, y, false, !strcmp(cmd, "hold") ? 900 : 90);
    Serial.println("OK");
  } else if (!strcmp(cmd, "swipe")) {
    int x0, y0, x1, y1;
    if (sscanf(arg ? arg : "", "%d %d %d %d", &x0, &y0, &x1, &y1) == 4) {
      nSteps = stepIdx = 0;
      for (int i = 0; i <= 10; i++) schedule(x0 + (x1 - x0) * i / 10, y0 + (y1 - y0) * i / 10, true, i ? 25 : 0);
      schedule(x1, y1, false, 25);
    }
    Serial.println("OK");
  } else if (!strcmp(cmd, "msg") && arg) {
    char *b64 = strchr(arg, ' ');
    uint8_t type = (uint8_t)strtol(arg, nullptr, 16);
    size_t outLen = 0;
    uint8_t *out = nullptr;
    if (b64) {
      b64++;
      size_t inLen = strlen(b64);
      out = (uint8_t *)heap_caps_malloc(inLen, MALLOC_CAP_SPIRAM);
      if (!out || mbedtls_base64_decode(out, inLen, &outLen, (const uint8_t *)b64, inLen) != 0) {
        free(out);
        Serial.println("ERR base64");
        return;
      }
    }
    phone::enqueue(type, out, outLen);
    free(out);
    Serial.printf("OK %u\n", (unsigned)outLen);
  } else if (!strcmp(cmd, "page") && arg) {
    static const char *names[] = {"weather", "face", "map", "places", "activity", "quick", "notifications"};
    for (int i = 0; i < 7; i++)
      if (!strcmp(arg, names[i])) ui::goPage((ui::Page)i, false);
    Serial.println("OK");
  } else if (!strcmp(cmd, "refr")) {
    lv_display_t *d = lv_display_get_default();
    lv_obj_invalidate(lv_screen_active());
    uint32_t t0 = millis();
    lv_refr_now(d);
    Serial.printf("OK refr disp=%p took=%lums flushes=%lu tick=%lu millis=%lu\n", d, (unsigned long)(millis() - t0),
                  (unsigned long)hal::flushes(), (unsigned long)lv_tick_get(), (unsigned long)millis());
  } else if (!strcmp(cmd, "wake")) {
    hal::screenOn(true);
    hal::noteActivity();
    Serial.println("OK");
  } else if (!strcmp(cmd, "sleep")) {
    hal::screenOn(false);
    Serial.println("OK");
  } else if (!strcmp(cmd, "info")) {
    struct tm lt;
    hal::localTime(&lt);
    Serial.printf("INFO fw=%s page=%s bat=%d mv=%d chg=%d usb=%d steps=%lu ble=%d mtu=%d map=%d feats=%d route=%d gps=%d heap=%u psram=%u flushes=%lu fpx=%lu scr=%d tz=%ld local=%02d:%02d\n",
                  FW_VERSION, ui::pageName(), hal::batteryPercent(), hal::batteryMillivolts(), hal::charging(),
                  hal::usbPowered(), (unsigned long)hal::stepsToday(), phone::connected(), phone::mtu(),
                  mapdata::hasMap(), mapdata::featureCount(), mapdata::routePoints(), app.gps.valid,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), (unsigned long)hal::flushes(),
                  (unsigned long)hal::flushedPixels(), hal::screenIsOn(), (long)hal::tzOffset(), lt.tm_hour, lt.tm_min);
  } else if (cmd[0]) {
    Serial.println("ERR unknown");
  }
}

}  // namespace

namespace console {

void begin() { line = (char *)heap_caps_malloc(kLineMax, MALLOC_CAP_SPIRAM); }

void loop() {
  // scripted touches
  while (stepIdx < nSteps && millis() >= steps[stepIdx].at) {
    const Step &s = steps[stepIdx++];
    hal::injectTouch(s.x, s.y, s.pressed);
  }
  if (!line) return;
  int budget = 64 * 1024;
  while (Serial.available() && budget-- > 0) {
    char c = Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      line[lineLen] = 0;
      handle(line);
      lineLen = 0;
      continue;
    }
    if (lineLen < kLineMax - 1) line[lineLen++] = c;
  }
}

}  // namespace console
