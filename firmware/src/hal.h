// Hardware layer for the Waveshare ESP32-S3-Touch-AMOLED-2.06.
#pragma once
#include <Arduino.h>
#include <time.h>

namespace hal {

constexpr int W = 410;
constexpr int H = 502;

bool begin();
void loop();  // polls PMU, IMU and buttons; call every main-loop pass

// Display / LVGL glue
void lvglBegin();
uint16_t *shadowFramebuffer();  // RGB565 copy of everything flushed to the panel
uint32_t flushes();
uint32_t flushedPixels();
void screenOn(bool on);
bool screenIsOn();
void setBrightness(uint8_t level);  // 0..255
uint8_t brightness();
void noteActivity();                 // resets the screen-off timer
uint32_t idleMs();

// Touch (debug console can inject synthetic touches)
void injectTouch(int x, int y, bool pressed);

// Power
int batteryPercent();
int batteryMillivolts();
bool charging();
bool usbPowered();

// Charging
bool takeUsbPlugged();   // edge: USB power just connected
bool takeChargeFull();   // edge: battery just reached full
bool chargeComplete();
int chargeEtaMinutes();  // -1 while unknown
void setPowerSaver(bool on);  // caps brightness (low battery)
bool powerSaverOn();

// Motion
void setRaiseToWake(bool on);
void accel(float &x, float &y, float &z);
uint32_t stepsToday();
void resetSteps();

// Time: system clock is UTC; tzOffset is applied for display.
void setUtc(time_t utc);
void setTzOffset(int32_t seconds);
int32_t tzOffset();
bool timeValid();
void localTime(struct tm *out);

// Buttons (edge-triggered, cleared when read)
bool takeBootPress();
bool takePowerPress();

}  // namespace hal
