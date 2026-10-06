#include "hal.h"

#include <Wire.h>
#include <Preferences.h>
#include <sys/time.h>
#include <lvgl.h>

#include "Arduino_GFX_Library.h"
#define XPOWERS_CHIP_AXP2101
#include "XPowersLib.h"
#include "SensorPCF85063.hpp"
#include "SensorQMI8658.hpp"

// ---- Pins (Waveshare ESP32-S3-Touch-AMOLED-2.06) ----
static constexpr int PIN_LCD_D0 = 4, PIN_LCD_D1 = 5, PIN_LCD_D2 = 6, PIN_LCD_D3 = 7;
static constexpr int PIN_LCD_SCLK = 11, PIN_LCD_CS = 12, PIN_LCD_RST = 8;
static constexpr int PIN_SDA = 15, PIN_SCL = 14;
static constexpr int PIN_TP_RST = 9;
static constexpr int PIN_BOOT = 0;
static constexpr uint8_t FT3168_ADDR = 0x38;

static Arduino_DataBus *bus = new Arduino_ESP32QSPI(PIN_LCD_CS, PIN_LCD_SCLK, PIN_LCD_D0, PIN_LCD_D1, PIN_LCD_D2, PIN_LCD_D3);
static Arduino_CO5300 *gfx = new Arduino_CO5300(bus, PIN_LCD_RST, 0, hal::W, hal::H, 22, 0, 0, 0);

static XPowersPMU pmu;
static SensorPCF85063 rtc;
static SensorQMI8658 imu;
static bool pmuOk = false, rtcOk = false, imuOk = false;

static uint16_t *shadowFb = nullptr;
static volatile uint32_t flushCount = 0, flushPixels = 0;
static bool screenOnState = true;
static uint8_t brightnessLevel = 200;
static uint32_t lastActivityMs = 0;
static bool swallowTouch = false;  // first touch after wake only wakes the screen

static volatile bool injActive = false;
static volatile int injX = 0, injY = 0;
static volatile bool injPressed = false;

static int batPct = -1, batMv = 0;
static bool isCharging = false, isUsb = false;
static bool bootEdge = false, pwrEdge = false, usbEdge = false, fullEdge = false;
static bool chargeDone = false;
static bool powerSaver = false, raiseToWake = true;
static uint32_t rampStartMs = 0;
static float accX = 0, accY = 0, accZ = 0;
// charge-rate tracking for the "full in N min" estimate
static uint32_t chgRefMs = 0;
static int chgRefPct = -1;
static int chgEtaMin = -1;
static int32_t tzOffsetSec = 0;
static Preferences prefs;

// ---- steps (simple peak detector on accel magnitude, ~50 Hz) ----
static uint32_t stepCount = 0;
static int stepDay = -1;
static float accLp = 1.0f, accBase = 1.0f;
static bool stepArmed = true;
static uint32_t lastStepMs = 0;

namespace hal {

uint16_t *shadowFramebuffer() { return shadowFb; }
uint32_t flushes() { return flushCount; }
uint32_t flushedPixels() { return flushPixels; }

static bool touchReadRaw(int &x, int &y) {
  Wire.beginTransmission(FT3168_ADDR);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)FT3168_ADDR, 5) != 5) return false;
  uint8_t n = Wire.read() & 0x0F;
  uint8_t xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
  if (n == 0 || n > 2) return false;
  x = ((xh & 0x0F) << 8) | xl;
  y = ((yh & 0x0F) << 8) | yl;
  return x < W && y < H;
}

void injectTouch(int x, int y, bool pressed) {
  injX = x;
  injY = y;
  injPressed = pressed;
  injActive = pressed;
}

static void lvTouchRead(lv_indev_t *, lv_indev_data_t *data) {
  static int lastX = 0, lastY = 0;
  int x, y;
  bool pressed;
  if (injActive || injPressed) {
    x = injX;
    y = injY;
    pressed = injPressed;
  } else {
    pressed = touchReadRaw(x, y);
  }
  if (pressed) {
    lastActivityMs = millis();
    if (!screenOnState) {
      screenOn(true);
      swallowTouch = true;
    }
  } else {
    swallowTouch = false;
  }
  if (pressed && !swallowTouch) {
    lastX = x;
    lastY = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
  data->point.x = lastX;
  data->point.y = lastY;
}

static void lvFlush(lv_display_t *disp, const lv_area_t *area, uint8_t *px) {
  const int w = lv_area_get_width(area), h = lv_area_get_height(area);
  flushCount++;
  flushPixels += w * h;
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)px, w, h);
  if (shadowFb) {
    const uint16_t *src = (const uint16_t *)px;
    for (int row = 0; row < h; row++)
      memcpy(shadowFb + (area->y1 + row) * W + area->x1, src + row * w, w * 2);
  }
  lv_display_flush_ready(disp);
}

// CO5300 needs even start / odd end coordinates.
static void lvRounder(lv_event_t *e) {
  lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
  a->x1 &= ~1;
  a->y1 &= ~1;
  a->x2 |= 1;
  a->y2 |= 1;
}

void lvglBegin() {
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  constexpr int LINES = 52;  // 2 x 42 KB in internal RAM
  const size_t bufBytes = W * LINES * 2;
  void *buf1 = heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  void *buf2 = heap_caps_malloc(bufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
  lv_display_t *disp = lv_display_create(W, H);
  lv_display_set_flush_cb(disp, lvFlush);
  Serial.printf("[hal] lvgl draw buffers %p %p (%u bytes)\n", buf1, buf2, (unsigned)bufBytes);
  lv_display_set_buffers(disp, buf1, buf2, bufBytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_add_event_cb(disp, lvRounder, LV_EVENT_INVALIDATE_AREA, nullptr);

  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, lvTouchRead);
  lv_indev_set_scroll_limit(indev, 12);
  lv_indev_set_long_press_time(indev, 600);
}

bool begin() {
  pinMode(PIN_BOOT, INPUT_PULLUP);
  prefs.begin("hal", false);
  tzOffsetSec = prefs.getInt("tz", 0);
  brightnessLevel = prefs.getUChar("bright", 200);

  if (!gfx->begin(80000000)) Serial.println("[hal] display init failed");
  gfx->fillScreen(0x0000);
  gfx->setBrightness(brightnessLevel);
  shadowFb = (uint16_t *)heap_caps_calloc(W * H, 2, MALLOC_CAP_SPIRAM);

  Wire.begin(PIN_SDA, PIN_SCL, 400000);

  pinMode(PIN_TP_RST, OUTPUT);
  digitalWrite(PIN_TP_RST, LOW);
  delay(10);
  digitalWrite(PIN_TP_RST, HIGH);
  delay(120);

  pmuOk = pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, PIN_SDA, PIN_SCL);
  if (pmuOk) {
    pmu.disableTSPinMeasure();  // no thermistor on this board; enabling it blocks charging
    pmu.enableBattDetection();
    pmu.enableBattVoltageMeasure();
    pmu.enableVbusVoltageMeasure();
    pmu.enableSystemVoltageMeasure();
    pmu.setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);
    pmu.disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
    pmu.clearIrqStatus();
    pmu.enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ);
  } else {
    Serial.println("[hal] AXP2101 not found");
  }

  rtcOk = rtc.begin(Wire, PIN_SDA, PIN_SCL);
  if (rtcOk) {
    RTC_DateTime dt = rtc.getDateTime();
    if (dt.getYear() >= 2025 && dt.getYear() < 2100) {
      struct tm t = dt.toUnixTime();
      time_t utc = mktime(&t);  // TZ env is UTC, so mktime == timegm
      struct timeval tv = {utc, 0};
      settimeofday(&tv, nullptr);
    }
  } else {
    Serial.println("[hal] PCF85063 not found");
  }

  imuOk = imu.begin(Wire, QMI8658_L_SLAVE_ADDRESS, PIN_SDA, PIN_SCL);
  if (imuOk) {
    imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G, SensorQMI8658::ACC_ODR_62_5Hz, SensorQMI8658::LPF_MODE_0);
    imu.enableAccelerometer();
  } else {
    Serial.println("[hal] QMI8658 not found");
  }
  stepCount = prefs.getUInt("steps", 0);
  stepDay = prefs.getInt("stepday", -1);

  lastActivityMs = millis();
  return true;
}

// Raise-to-wake: wrist was hanging or tilted away (screen not facing up), then turns face-up and holds.
static void pollRaise(float ax, float ay, float az) {
  static float zLp = 0;
  static uint32_t awayMs = 0, upSinceMs = 0;
  zLp += (az - zLp) * 0.3f;
  uint32_t now = millis();
  if (zLp < 0.35f) awayMs = now;  // screen pointing sideways/down
  bool faceUp = zLp > 0.75f && fabsf(ax) < 0.55f;
  if (!faceUp) {
    upSinceMs = 0;
    return;
  }
  if (!upSinceMs) upSinceMs = now;
  // turned up within the last 1.2 s and held for 150 ms
  if (raiseToWake && !screenOnState && now - awayMs < 1200 && now - upSinceMs > 150) {
    hal::screenOn(true);
    awayMs = 0;
  }
}

static void pollSteps() {
  static uint32_t lastSample = 0;
  if (!imuOk || millis() - lastSample < 20) return;
  lastSample = millis();
  float ax, ay, az;
  if (!imu.getAccelerometer(ax, ay, az)) return;
  accX = ax;
  accY = ay;
  accZ = az;
  pollRaise(ax, ay, az);
  float mag = sqrtf(ax * ax + ay * ay + az * az);
  accLp += (mag - accLp) * 0.35f;       // smooth
  accBase += (accLp - accBase) * 0.02f;  // slow baseline (~1g)
  float d = accLp - accBase;
  uint32_t now = millis();
  if (stepArmed && d > 0.13f && now - lastStepMs > 280) {
    stepArmed = false;
    if (now - lastStepMs < 2000) stepCount++;  // ignore isolated jolts
    lastStepMs = now;
  } else if (!stepArmed && d < 0.02f) {
    stepArmed = true;
  }
}

static void pollPower() {
  static uint32_t last = 0;
  if (!pmuOk || millis() - last < 100) return;
  last = millis();
  pmu.getIrqStatus();
  if (pmu.isPekeyShortPressIrq()) pwrEdge = true;
  pmu.clearIrqStatus();

  static uint32_t lastBat = 0;
  if (millis() - lastBat > 1000 || batPct < 0) {
    lastBat = millis();
    bool usb = pmu.isVbusIn();
    if (usb && !isUsb && batPct >= 0) usbEdge = true;
    isUsb = usb;
    isCharging = pmu.isCharging();
    batMv = pmu.getBattVoltage();
    batPct = pmu.isBatteryConnect() ? pmu.getBatteryPercent() : -1;
    bool done = isUsb && !isCharging && batPct >= 99;
    if (done && !chargeDone) fullEdge = true;
    chargeDone = done;

    // time-to-full from the charge rate over the last few minutes
    if (!isCharging || batPct < 0) {
      chgRefPct = -1;
      chgEtaMin = -1;
    } else if (chgRefPct < 0) {
      chgRefPct = batPct;
      chgRefMs = millis();
    } else if (batPct > chgRefPct) {
      float minutes = (millis() - chgRefMs) / 60000.0f;
      if (minutes > 2) chgEtaMin = (int)((100 - batPct) * minutes / (batPct - chgRefPct) + 0.5f);
      if (minutes > 15) {  // slide the window so the estimate follows the CC/CV curve
        chgRefPct = batPct;
        chgRefMs = millis();
      }
    }
  }
}

static uint8_t effectiveBrightness() { return powerSaver ? min<uint8_t>(brightnessLevel, 90) : brightnessLevel; }

void loop() {
  pollPower();
  pollSteps();

  // fade the panel in after waking (looks like a real watch instead of a hard switch-on)
  if (rampStartMs && screenOnState) {
    uint32_t t = millis() - rampStartMs;
    if (t >= 180) {
      gfx->setBrightness(effectiveBrightness());
      rampStartMs = 0;
    } else {
      gfx->setBrightness((uint8_t)(effectiveBrightness() * t / 180));
    }
  }

  static bool bootWasDown = false;
  bool bootDown = digitalRead(PIN_BOOT) == LOW;
  if (bootDown && !bootWasDown) bootEdge = true;
  bootWasDown = bootDown;

  // day rollover + persist steps once a minute
  static uint32_t lastSave = 0;
  if (millis() - lastSave > 60000) {
    lastSave = millis();
    if (timeValid()) {
      struct tm t;
      localTime(&t);
      if (stepDay != t.tm_yday) {
        if (stepDay != -1) stepCount = 0;
        stepDay = t.tm_yday;
        prefs.putInt("stepday", stepDay);
      }
    }
    prefs.putUInt("steps", stepCount);
  }
}

void screenOn(bool on) {
  if (on == screenOnState) return;
  screenOnState = on;
  if (on) {
    setCpuFrequencyMhz(240);
    gfx->setBrightness(0);
    gfx->displayOn();
    rampStartMs = millis() | 1;
    lastActivityMs = millis();
    lv_obj_invalidate(lv_screen_active());
  } else {
    gfx->setBrightness(0);
    gfx->displayOff();
    // Screen-off is most of a watch's life: 80 MHz is plenty for BLE, touch-to-wake and the step counter.
    setCpuFrequencyMhz(80);
  }
}
bool screenIsOn() { return screenOnState; }

void setBrightness(uint8_t level) {
  brightnessLevel = max<uint8_t>(level, 10);
  if (screenOnState) gfx->setBrightness(effectiveBrightness());
  prefs.putUChar("bright", brightnessLevel);
}
uint8_t brightness() { return brightnessLevel; }

void setPowerSaver(bool on) {
  if (on == powerSaver) return;
  powerSaver = on;
  if (screenOnState) gfx->setBrightness(effectiveBrightness());
}
bool powerSaverOn() { return powerSaver; }
void setRaiseToWake(bool on) { raiseToWake = on; }
bool takeUsbPlugged() {
  bool e = usbEdge;
  usbEdge = false;
  return e;
}
bool takeChargeFull() {
  bool e = fullEdge;
  fullEdge = false;
  return e;
}
bool chargeComplete() { return chargeDone; }
int chargeEtaMinutes() { return chgEtaMin; }
void accel(float &x, float &y, float &z) {
  x = accX;
  y = accY;
  z = accZ;
}
void noteActivity() { lastActivityMs = millis(); }
uint32_t idleMs() { return millis() - lastActivityMs; }

int batteryPercent() { return batPct; }
int batteryMillivolts() { return batMv; }
bool charging() { return isCharging; }
bool usbPowered() { return isUsb; }

uint32_t stepsToday() { return stepCount; }
void resetSteps() { stepCount = 0; }

void setUtc(time_t utc) {
  struct timeval tv = {utc, 0};
  settimeofday(&tv, nullptr);
  if (rtcOk) {
    struct tm t;
    gmtime_r(&utc, &t);
    rtc.setDateTime(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
  }
}
void setTzOffset(int32_t s) {
  if (s == tzOffsetSec) return;
  tzOffsetSec = s;
  prefs.putInt("tz", s);
}
int32_t tzOffset() { return tzOffsetSec; }
bool timeValid() { return time(nullptr) > 1700000000; }
void localTime(struct tm *out) {
  time_t t = time(nullptr) + tzOffsetSec;
  gmtime_r(&t, out);
}

bool takeBootPress() {
  bool e = bootEdge;
  bootEdge = false;
  return e;
}
bool takePowerPress() {
  bool e = pwrEdge;
  pwrEdge = false;
  return e;
}

}  // namespace hal
