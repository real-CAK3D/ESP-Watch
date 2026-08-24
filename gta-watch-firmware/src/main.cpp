#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Arduino_DriveBus_Library.h>
#include <ChronosESP32.h>
#include <FS.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <Wire.h>
#include "HWCDC.h"
#include "gta_map_watch_close.h"
#include "gta_map_source.h"
#include "lewiston_osm_areas.h"
#include "lewiston_osm_roads.h"
#include "pin_config.h"
#include "watch_launcher_assets.h"
#include "watch_icon_masks.h"

#define GTA_NAV_TOUCH_DIAG 0

HWCDC USBSerial;

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

Arduino_GFX *gfx = new Arduino_CO5300(
    bus, LCD_RESET, 0, LCD_WIDTH, LCD_HEIGHT, 22, 0, 0, 0);

std::shared_ptr<Arduino_IIC_DriveBus> iicBus =
    std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);

void touchInterrupt();

std::unique_ptr<Arduino_IIC> touch(new Arduino_FT3x68(
    iicBus, FT3168_DEVICE_ADDRESS, TP_RESET, TP_INT, touchInterrupt));

ChronosESP32 watch("GTA-Nav", CF_WAVESHARE_410x502);
Preferences prefs;

enum Screen {
  SCREEN_LAUNCHER = 0,
  SCREEN_MAP,
  SCREEN_NOTIFICATIONS,
  SCREEN_MESSAGE_DETAIL,
  SCREEN_PHONE,
  SCREEN_RECENTS,
  SCREEN_GALLERY,
  SCREEN_CLOCK,
  SCREEN_SETTINGS,
  SCREEN_BUDDY,
  SCREEN_PLACES,
  SCREEN_MECHANIC,
  SCREEN_WEATHER,
  SCREEN_FITNESS,
  SCREEN_CALENDAR,
  SCREEN_ALARMS,
  SCREEN_ABOUT,
  SCREEN_COUNT
};

struct Road {
  int x1;
  int y1;
  int x2;
  int y2;
  uint16_t color;
  uint8_t width;
};

struct Block {
  int x;
  int y;
  int w;
  int h;
  uint16_t color;
};

struct Place {
  const char *label;
  int x;
  int y;
  uint16_t color;
};

struct WatchContact {
  const char *name;
  const char *number;
  const char *role;
};

struct GpsFix {
  bool valid;
  float lat;
  float lon;
  float speed;
  float heading;
  unsigned long updatedMs;
};

struct CustomPlace {
  bool valid;
  char label[18];
  char icon[16];
  float lat;
  float lon;
  uint16_t color;
};

struct SyncedContact {
  bool valid;
  char name[24];
  char number[22];
  char role[28];
};

struct SyncedCall {
  bool valid;
  char name[24];
  char number[22];
  char kind[14];
};

struct SyncedMessage {
  bool valid;
  char from[24];
  char text[64];
};

struct SyncedPhoto {
  bool valid;
  char album[24];
  char label[32];
  char date[16];
};

struct TripInfo {
  bool driving;
  float routeMiles;
  float tollCost;
  float gasPrice;
  float mpg;
  float tankGallons;
  char fuelType[12];
  char vehicle[22];
  char destination[24];
};

struct PhoneProfile {
  char home[34];
  char work[34];
  char timezone[22];
  char dateText[16];
  char alarmLabel[20];
  char alarmTime[8];
  bool alarmEnabled;
  long timezoneOffsetMinutes;
  unsigned long syncedAtMs;
  unsigned long epochAtSync;
};

struct PhoneWeather {
  bool valid;
  int temp;
  int high;
  int low;
  char label[18];
  char city[24];
  unsigned long updatedMs;
};

struct PhoneFitness {
  uint32_t steps;
  float distanceMiles;
  uint16_t calories;
  uint16_t activeMinutes;
  uint32_t goal;
  bool valid;
  unsigned long updatedMs;
};

static const uint16_t COL_BLACK = 0x0000;
static const uint16_t COL_PANEL = 0x2104;
static const uint16_t COL_PANEL_2 = 0x3186;
static const uint16_t COL_TEXT = 0xFFDE;
static const uint16_t COL_MUTED = 0x9CF3;
static const uint16_t COL_ROUTE = 0xFEA0;
static const uint16_t COL_PLAYER = 0xFFFF;
static const uint16_t COL_SAFE = 0x47F1;
static const uint16_t COL_RED = 0xF9E7;
static const uint16_t COL_WATER = 0x4C74;
static const uint16_t COL_GRASS = 0xC5F1;
static const uint16_t COL_ASPHALT = 0xB596;
static const uint16_t COL_ROAD = 0x6B4D;
static const uint16_t COL_ROAD_DARK = 0x4A69;
static const uint16_t COL_ROAD_LIGHT = 0x8C71;
static const uint16_t COL_BLOCK = 0x94B2;
static const uint16_t COL_MAP_BG = 0x18C3;
static const uint16_t COL_MAP_GRID = 0x39E7;
static const uint16_t COL_MSG_BG = 0x1008;
static const uint16_t COL_PHONE_BG = 0x0008;
static const uint16_t COL_CLOCK_BG = 0x1808;
static const uint16_t COL_SETTINGS_BG = 0x2104;
static const uint16_t COL_MAP_TAN = 0x0000;
static const uint16_t COL_MAP_TAN_DARK = 0x18E3;
static const uint16_t COL_MAP_BLOCK = 0x18E3;
static const uint16_t COL_MAP_ROAD = 0xBDF7;
static const uint16_t COL_MAP_ROAD_MINOR = 0xAD75;
static const uint16_t COL_MAP_ROAD_LOCAL = 0x73AE;
static const uint16_t COL_MAP_PARK = 0x7C68;
static const uint16_t COL_MAP_WATER = 0x7456;
static const uint16_t COL_MAP_OUTLINE = 0x0000;
static const uint16_t COL_EDGE = 0x4A49;
static const uint16_t COL_EDGE_BRIGHT = 0x9CF3;

static const Road roads[] = {
    {-180, 102, 70, 125, COL_ROAD_LIGHT, 18}, {70, 125, 198, 92, COL_ROAD_LIGHT, 18},
    {198, 92, 312, 132, COL_ROAD_LIGHT, 18},  {312, 132, 560, 78, COL_ROAD_LIGHT, 18},
    {190, -170, 181, 72, COL_ROAD, 15},       {181, 72, 220, 184, COL_ROAD, 15},
    {220, 184, 164, 298, COL_ROAD, 15},       {164, 298, 226, 468, COL_ROAD, 15},
    {26, -26, 116, 124, COL_ROAD, 10},        {116, 124, 138, 256, COL_ROAD, 10},
    {138, 256, 94, 462, COL_ROAD, 10},        {-140, 362, 82, 318, COL_ROAD_DARK, 9},
    {82, 318, 176, 350, COL_ROAD_DARK, 9},    {176, 350, 322, 304, COL_ROAD_DARK, 9},
    {322, 304, 562, 350, COL_ROAD_DARK, 9},   {328, -60, 274, 92, COL_ROAD, 12},
    {274, 92, 292, 202, COL_ROAD, 12},        {292, 202, 348, 284, COL_ROAD, 12},
    {348, 284, 326, 512, COL_ROAD, 12},       {326, 512, 418, 636, COL_ROAD, 12},
    {-80, 232, 88, 220, COL_ROAD_DARK, 7},    {88, 220, 236, 248, COL_ROAD_DARK, 7},
    {236, 248, 502, 232, COL_ROAD_DARK, 7},   {20, 470, 162, 422, COL_ROAD_DARK, 7},
};

static const Block blocks[] = {
    {28, 170, 76, 52, 0x52AA},  {306, 152, 52, 96, 0x4208},
    {254, 336, 84, 48, 0x52AA}, {46, 26, 84, 44, 0x632C},
    {118, 364, 52, 94, 0x4208}, {326, 28, 50, 78, 0x5AEB},
    {228, 20, 44, 50, 0x4A49},  {42, 262, 52, 44, 0x3A07},
    {354, 382, 42, 70, 0x4A49}, {210, 420, 72, 38, 0x632C},
    {132, 178, 42, 46, 0x39E7}, {372, 176, 66, 44, 0x4208},
};

static const Place places[] = {
    {"Safe House", 70, 402, COL_SAFE},
    {"Garage", 142, 314, 0x7E7F},
    {"Ammu-Nation", 318, 170, COL_RED},
    {"Burger Shot", 348, 338, 0xFEA0},
    {"Pay 'n' Spray", 216, 104, 0x66FF},
    {"Cluckin' Bell", 96, 198, 0xFEA0},
    {"Maze Bank", 292, 276, COL_SAFE},
    {"Fleeca", 338, 78, COL_SAFE},
    {"Cool Beans", 174, 226, 0xD5B1},
    {"Taco Bomb", 66, 88, 0xFEA0},
    {"24/7", 370, 422, COL_ROUTE},
    {"Waypoint", 384, 92, COL_ROUTE},
};

static const int PLACE_COUNT = sizeof(places) / sizeof(places[0]);
static const int CUSTOM_PLACE_COUNT = 12;
static const int SYNC_CONTACT_COUNT = 12;
static const int SYNC_CALL_COUNT = 8;
static const int SYNC_MESSAGE_COUNT = 8;
static const int SYNC_PHOTO_COUNT = 12;
static const WatchContact gtaContacts[] = {
    {"Mechanic", "+15551234567", "Find parked car"},
    {"Mors Mutual-ish", "+15552667700", "Insurance claims"},
    {"Los Santos Customs", "+15557772687", "Repairs and paint"},
    {"Maze Bank-ish", "+15556293265", "Budget alerts"},
    {"Downtown Cab-ish", "+15558294222", "Route pickup"},
    {"Roadside", "+18005551212", "Tow and help"},
};
static const int GTA_CONTACT_COUNT = sizeof(gtaContacts) / sizeof(gtaContacts[0]);

Screen activeScreen = SCREEN_LAUNCHER;
bool overviewMode = false;
bool sdMounted = false;
bool touchReady = false;
bool bleConnected = false;
bool lastTouchDown = false;
bool needsRedraw = true;
bool darkMapMode = true;
bool parkingSaved = false;
bool routeToParking = false;
uint8_t selectedPlace = 0;
int placesScroll = 0;
int touchStartX = 0;
int touchStartY = 0;
int touchLastX = 0;
int touchLastY = 0;
unsigned long touchStartMs = 0;
unsigned long lastTapMs = 0;
unsigned long lastMapTapMs = 0;
unsigned long lastClockDrawMs = 0;
unsigned long lastMapDrawMs = 0;
char lastClockMinuteText[16] = "";
bool touchSwipeConsumed = false;
bool touchMoved = false;
bool touchScrolled = false;
unsigned long lastScrollDrawMs = 0;
bool mapOverlayDirty = false;
bool mapBlipDirty = false;
bool mapBlipRectValid = false;
unsigned long lastHeadCommandMs = 0;
String rawCommandBuffer;
char lastRawCommand[64] = "";
unsigned long lastRawCommandMs = 0;
uint32_t rawCommandCount = 0;
uint32_t gpsCommandCount = 0;
uint32_t headCommandCount = 0;
uint32_t poiCommandCount = 0;
int mapBlipLastX = 0;
int mapBlipLastY = 0;
int mapBlipLastW = 0;
int mapBlipLastH = 0;
GpsFix liveGps = {false, 0.0f, 0.0f, 0.0f, 0.0f, 0};
GpsFix parkedGps = {false, 0.0f, 0.0f, 0.0f, 0.0f, 0};
GpsFix destinationGps = {false, 0.0f, 0.0f, 0.0f, 0.0f, 0};
CustomPlace customPlaces[CUSTOM_PLACE_COUNT];
SyncedContact syncedContacts[SYNC_CONTACT_COUNT];
SyncedCall syncedCalls[SYNC_CALL_COUNT];
SyncedMessage syncedMessages[SYNC_MESSAGE_COUNT];
SyncedPhoto syncedPhotos[SYNC_PHOTO_COUNT];
bool gpsAnchorValid = false;
float gpsAnchorLat = 0.0f;
float gpsAnchorLon = 0.0f;
TripInfo trip = {true, 0.0f, 0.0f, 3.49f, 24.0f, 14.0f, "Regular", "Vehicle", "Waypoint"};
PhoneProfile profile = {"Safe House", "Work", "Phone TZ", "", "Alarm", "07:00", false, 0, 0, 0};
PhoneWeather phoneWeather = {false, 0, 0, 0, "Weather", "Phone", 0};
PhoneFitness phoneFitness = {0, 0.0f, 0, 0, 8000, false, 0};
uint8_t activeTheme = 1;
uint8_t clockFaceId = 0;
uint8_t launcherPage = 0;
uint8_t buddyView = 0;
char buddyLine[96] = "Phone mic bridge ready.";
uint16_t mapLine[LCD_WIDTH];
bool qmiReady = false;
uint32_t localSteps = 0;

void drawTopBar();
void printAt(int x, int y, String text, uint16_t color, uint8_t size = 1);
void thickLine(int x1, int y1, int x2, int y2, uint16_t color, int width);
void drawMapScreen();
void drawWideMapHud();
int syncedCallCount();
int syncedMessageCount();

void touchInterrupt() {
  touch->IIC_Interrupt_Flag = true;
}

void pollLocalPedometer() {
  qmiReady = false;
  localSteps = 0;
}

String rewritePlaceTerms(String text) {
  text.replace("Home", "Safe House");
  text.replace("home", "Safe House");
  text.replace("My house", "Safe House");
  text.replace("my house", "Safe House");
  text.replace("McDonald's", "Cluckin' Bell");
  text.replace("Mcdonalds", "Cluckin' Bell");
  text.replace("KFC", "Cluckin' Bell");
  text.replace("Popeyes", "Cluckin' Bell");
  text.replace("Burger King", "Burger Shot");
  text.replace("Wendy's", "Burger Shot");
  text.replace("Five Guys", "Burger Shot");
  text.replace("Taco Bell", "Taco Bomb");
  text.replace("Starbucks", "Cool Beans");
  text.replace("Dunkin", "Cool Beans");
  text.replace("Car Wash", "Pay 'n' Spray");
  text.replace("car wash", "Pay 'n' Spray");
  text.replace("Auto Wash", "Pay 'n' Spray");
  text.replace("auto wash", "Pay 'n' Spray");
  text.replace("Bank of America", "Maze Bank");
  text.replace("Chase", "Maze Bank");
  text.replace("TD Bank", "Fleeca");
  text.replace("credit union", "Fleeca");
  text.replace("Credit Union", "Fleeca");
  text.replace("Post Office", "Post OP");
  text.replace("post office", "Post OP");
  text.replace("Police", "LSPD");
  text.replace("police", "LSPD");
  text.replace("Hospital", "Mount Zonah");
  text.replace("hospital", "Mount Zonah");
  return text;
}

String fit(String text, uint8_t maxChars) {
  if (text.length() <= maxChars) {
    return text;
  }
  if (maxChars <= 3) {
    return text.substring(0, maxChars);
  }
  return text.substring(0, maxChars - 3) + "...";
}

int gpsToMapX(float lon) {
  if (!liveGps.valid) {
    return 205;
  }
  return constrain(205 + (int)((lon - liveGps.lon) * 900000.0f), 20, LCD_WIDTH - 20);
}

int gpsToMapY(float lat) {
  if (!liveGps.valid) {
    return 251;
  }
  return constrain(251 - (int)((lat - liveGps.lat) * 900000.0f), 56, LCD_HEIGHT - 114);
}

String gpsLine(const GpsFix &fix) {
  if (!fix.valid) {
    return "GPS waiting";
  }
  String line = String(fix.lat, 6) + ", " + String(fix.lon, 6);
  if (fix.speed > 0.1f) {
    line += "  ";
    line += String(fix.speed, 1);
    line += " mph";
  }
  return line;
}

uint32_t effectiveSteps() {
  if (qmiReady) return localSteps;
  if (phoneFitness.valid) return phoneFitness.steps;
  return 0;
}

uint32_t effectiveStepGoal() {
  return phoneFitness.goal > 0 ? phoneFitness.goal : 8000;
}

float effectiveDistanceMiles() {
  if (qmiReady) return localSteps * 0.00047f;
  return phoneFitness.valid ? phoneFitness.distanceMiles : 0.0f;
}

uint16_t effectiveCalories() {
  if (qmiReady) return (uint16_t)min(65535UL, (unsigned long)(localSteps * 0.045f));
  return phoneFitness.valid ? phoneFitness.calories : 0;
}

uint16_t effectiveActiveMinutes() {
  if (qmiReady) return (uint16_t)min(65535UL, (unsigned long)(localSteps / 110));
  return phoneFitness.valid ? phoneFitness.activeMinutes : 0;
}

String fitnessSourceText() {
  if (qmiReady) return "Watch pedometer live";
  if (phoneFitness.valid) return "Phone activity synced";
  return "Waiting for phone or IMU";
}

bool gpsFresh() {
  return liveGps.valid && millis() - liveGps.updatedMs < 7000;
}

float gpsDistanceMiles(float lat1, float lon1, float lat2, float lon2) {
  const float rMiles = 3958.7613f;
  float dLat = (lat2 - lat1) * PI / 180.0f;
  float dLon = (lon2 - lon1) * PI / 180.0f;
  float a = sin(dLat / 2.0f) * sin(dLat / 2.0f) +
            cos(lat1 * PI / 180.0f) * cos(lat2 * PI / 180.0f) *
                sin(dLon / 2.0f) * sin(dLon / 2.0f);
  float c = 2.0f * atan2(sqrt(a), sqrt(1.0f - a));
  return rMiles * c;
}

float routeMilesRemaining() {
  if (liveGps.valid && destinationGps.valid) {
    return gpsDistanceMiles(liveGps.lat, liveGps.lon, destinationGps.lat, destinationGps.lon);
  }
  if (routeToParking && liveGps.valid && parkedGps.valid) {
    return gpsDistanceMiles(liveGps.lat, liveGps.lon, parkedGps.lat, parkedGps.lon);
  }
  return trip.routeMiles;
}

bool phoneTimeValid() {
  return profile.syncedAtMs > 0 && profile.epochAtSync > 1600000000UL;
}

unsigned long phoneEpochNow() {
  if (!phoneTimeValid()) {
    return 0;
  }
  return profile.epochAtSync + ((millis() - profile.syncedAtMs) / 1000UL);
}

void datePartsFromEpoch(unsigned long epoch, int &year, int &month, int &day, int &hour, int &minute, int &second) {
  unsigned long days = epoch / 86400UL;
  unsigned long rem = epoch % 86400UL;
  hour = rem / 3600UL;
  rem %= 3600UL;
  minute = rem / 60UL;
  second = rem % 60UL;

  year = 1970;
  while (true) {
    bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    int daysInYear = leap ? 366 : 365;
    if (days < (unsigned long)daysInYear) {
      break;
    }
    days -= daysInYear;
    year++;
  }

  static const uint8_t monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  month = 1;
  for (int i = 0; i < 12; i++) {
    int dim = monthDays[i];
    if (i == 1 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
      dim = 29;
    }
    if (days < (unsigned long)dim) {
      day = days + 1;
      return;
    }
    days -= dim;
    month++;
  }
  day = 1;
}

String twoDigit(int value) {
  return value < 10 ? "0" + String(value) : String(value);
}

String bridgeTimeText(bool withSeconds = false) {
  if (!phoneTimeValid()) {
    return watch.getHourC() + watch.getTime(withSeconds ? ":%M:%S " : ":%M ") + watch.getAmPmC();
  }
  int year, month, day, hour, minute, second;
  datePartsFromEpoch(phoneEpochNow(), year, month, day, hour, minute, second);
  int hour12 = hour % 12;
  if (hour12 == 0) {
    hour12 = 12;
  }
  String out = String(hour12) + ":" + twoDigit(minute);
  if (withSeconds) {
    out += ":" + twoDigit(second);
  }
  out += hour >= 12 ? " PM" : " AM";
  return out;
}

String bridgeDateText() {
  if (!phoneTimeValid()) {
    return rewritePlaceTerms(watch.getTimeDate());
  }
  int year, month, day, hour, minute, second;
  datePartsFromEpoch(phoneEpochNow(), year, month, day, hour, minute, second);
  return String(year) + "-" + twoDigit(month) + "-" + twoDigit(day);
}

String bridgeDowText() {
  if (!phoneTimeValid()) {
    return fit(rewritePlaceTerms(watch.getTimeDate()), 16);
  }
  static const char *days[] = {"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};
  unsigned long dayIndex = (phoneEpochNow() / 86400UL) % 7UL;
  return String(days[dayIndex]);
}

float mapPixelsPerMeter() {
  return overviewMode ? 2.15f : 2.75f;
}

void currentRadarRect(int &x, int &y, int &w, int &h) {
  if (overviewMode) {
    x = 6;
    y = 58;
    w = LCD_WIDTH - 12;
    h = 326;
  } else {
    x = 10;
    y = 48;
    w = 390;
    h = 390;
  }
}

bool radarIsCircular() {
  return !overviewMode;
}

void maskCircularRadarOutside(int x, int y, int w, int h) {
  if (!radarIsCircular()) {
    return;
  }
  int r = min(w, h) / 2;
  int cx = x + w / 2;
  int cy = y + h / 2;
  for (int row = y; row < y + h; row++) {
    int dy = row - cy;
    int inside = 0;
    int remaining = r * r - dy * dy;
    if (remaining > 0) {
      inside = (int)sqrt((float)remaining);
    }
    int leftEdge = constrain(cx - inside, x, x + w);
    int rightEdge = constrain(cx + inside, x, x + w);
    if (leftEdge > x) {
      gfx->drawFastHLine(x, row, leftEdge - x, COL_BLACK);
    }
    if (rightEdge < x + w) {
      gfx->drawFastHLine(rightEdge, row, x + w - rightEdge, COL_BLACK);
    }
  }
}

void drawArcBand(int cx, int cy, int radius, int startDeg, int endDeg, uint16_t color, int width) {
  int lastX = cx + (int)(cos(startDeg * 0.017453292f) * radius);
  int lastY = cy + (int)(sin(startDeg * 0.017453292f) * radius);
  for (int deg = startDeg + 1; deg <= endDeg; deg++) {
    float a = deg * 0.017453292f;
    int px = cx + (int)(cos(a) * radius);
    int py = cy + (int)(sin(a) * radius);
    thickLine(lastX, lastY, px, py, color, width);
    lastX = px;
    lastY = py;
  }
}

void drawRadarHouseIcon(int cx, int cy) {
  gfx->fillCircle(cx, cy, 17, COL_TEXT);
  gfx->fillCircle(cx, cy, 12, COL_BLACK);
  gfx->fillTriangle(cx - 9, cy - 1, cx, cy - 9, cx + 9, cy - 1, COL_TEXT);
  gfx->fillRect(cx - 7, cy - 1, 14, 10, COL_TEXT);
  gfx->fillRect(cx - 3, cy + 3, 6, 6, COL_BLACK);
  gfx->drawLine(cx - 9, cy - 1, cx, cy - 9, COL_BLACK);
  gfx->drawLine(cx, cy - 9, cx + 9, cy - 1, COL_BLACK);
}

void drawRadarFrame(int x, int y, int w, int h) {
  if (radarIsCircular()) {
    int r = min(w, h) / 2;
    int cx = x + w / 2;
    int cy = y + h / 2;
    for (int i = 0; i < 5; i++) {
      gfx->drawCircle(cx, cy, r - i, COL_BLACK);
    }
    drawArcBand(cx, cy, r - 7, 118, 303, 0x6BCA, 8);
    drawArcBand(cx, cy, r - 7, -58, 113, 0x4D76, 8);
    for (int i = 0; i < 3; i++) {
      gfx->drawCircle(cx, cy, r - 10 - i, COL_BLACK);
    }
    drawRadarHouseIcon(x + 56, y + 60);
    drawRadarHouseIcon(x + 21, cy + 2);
    drawRadarHouseIcon(cx, y + h - 18);
  } else {
    gfx->drawRect(x, y, w, h, COL_BLACK);
    gfx->drawRect(x + 1, y + 1, w - 2, h - 2, COL_ROUTE);
    gfx->fillRect(x + 8, y + h - 34, w - 16, 18, 0x2965);
    gfx->drawRect(x + 8, y + h - 34, w - 16, 18, COL_BLACK);
    gfx->fillRect(x + 12, y + h - 31, (w - 24) * 2 / 3, 12, COL_SAFE);
    printAt(x + 12, y + h - 58, liveGps.valid ? "1.04 km" : "-- km", COL_TEXT, 2);
    printAt(x + 8, y + 12, "N", COL_TEXT, 1);
  }
}

void drawRadarWaterPatches(int x, int y, int w, int h) {
  if (!radarIsCircular()) {
    return;
  }
  int cx = x + w / 2;
  int cy = y + h / 2;
  gfx->fillCircle(cx - 92, cy + 18, 28, COL_MAP_WATER);
  gfx->fillCircle(cx - 112, cy + 40, 20, COL_MAP_WATER);
  gfx->fillRect(cx - 122, cy + 4, 32, 50, COL_MAP_WATER);
  gfx->fillCircle(cx + 128, cy - 84, 54, COL_MAP_WATER);
  gfx->fillRect(cx + 108, cy - 138, 80, 110, COL_MAP_WATER);
}

void drawRadarManMadePatches(int x, int y, int w, int h) {
  if (!radarIsCircular()) {
    return;
  }
  const Block urbanBlocks[] = {
      {24, 34, 74, 46, COL_MAP_BLOCK},   {116, 28, 42, 78, COL_MAP_BLOCK},
      {178, 34, 96, 52, COL_MAP_BLOCK},  {292, 34, 58, 102, COL_MAP_BLOCK},
      {42, 120, 74, 78, COL_MAP_BLOCK},  {136, 130, 50, 104, COL_MAP_BLOCK},
      {218, 116, 82, 74, COL_MAP_BLOCK}, {316, 154, 46, 92, COL_MAP_BLOCK},
      {54, 238, 68, 58, COL_MAP_BLOCK},  {150, 250, 94, 50, COL_MAP_BLOCK},
      {268, 236, 76, 84, COL_MAP_BLOCK}, {84, 326, 92, 44, COL_MAP_BLOCK},
      {214, 334, 58, 78, COL_MAP_BLOCK}, {304, 338, 52, 46, COL_MAP_PARK},
  };
  for (uint8_t i = 0; i < sizeof(urbanBlocks) / sizeof(urbanBlocks[0]); i++) {
    const Block &b = urbanBlocks[i];
    int bx = x + b.x;
    int by = y + b.y;
    gfx->fillRect(bx, by, b.w, b.h, b.color);
    gfx->drawRect(bx, by, b.w, b.h, COL_BLACK);
  }
}

float fuelCost() {
  if (!trip.driving || trip.mpg <= 0.1f) {
    return 0.0f;
  }
  return (trip.routeMiles / trip.mpg) * trip.gasPrice;
}

float tripCost() {
  return trip.driving ? fuelCost() + trip.tollCost : 0.0f;
}

String dollars(float value) {
  char out[16];
  snprintf(out, sizeof(out), "$%.2f", value);
  return String(out);
}

void setFixedText(char *dest, size_t size, String value) {
  value.trim();
  value.replace('_', ' ');
  if (!value.length()) {
    return;
  }
  strncpy(dest, value.c_str(), size - 1);
  dest[size - 1] = '\0';
}

const char *activeRouteLabel() {
  if (routeToParking && parkingSaved) {
    return "Parked Car";
  }
  return places[selectedPlace].label;
}

int activeRouteX() {
  if (routeToParking && parkingSaved) {
    return gpsToMapX(parkedGps.lon);
  }
  return places[selectedPlace].x;
}

int activeRouteY() {
  if (routeToParking && parkingSaved) {
    return gpsToMapY(parkedGps.lat);
  }
  return places[selectedPlace].y;
}

void saveParkingFromCurrentFix() {
  parkingSaved = true;
  parkedGps.valid = true;
  if (liveGps.valid) {
    parkedGps = liveGps;
  } else {
    parkedGps.lat = 0.0f;
    parkedGps.lon = 0.0f;
    parkedGps.speed = 0.0f;
    parkedGps.heading = 0.0f;
    parkedGps.updatedMs = millis();
  }
  prefs.putBool("parked", parkingSaved);
  prefs.putFloat("parkLat", parkedGps.lat);
  prefs.putFloat("parkLon", parkedGps.lon);
  prefs.putFloat("parkHead", parkedGps.heading);
  routeToParking = true;
}

void loadParking() {
  parkingSaved = prefs.getBool("parked", false);
  parkedGps.valid = parkingSaved;
  parkedGps.lat = prefs.getFloat("parkLat", 0.0f);
  parkedGps.lon = prefs.getFloat("parkLon", 0.0f);
  parkedGps.heading = prefs.getFloat("parkHead", 0.0f);
  parkedGps.speed = 0.0f;
  parkedGps.updatedMs = 0;
}

void loadTripDefaults() {
  trip.tankGallons = prefs.getFloat("tankGal", trip.tankGallons);
  trip.mpg = prefs.getFloat("mpg", trip.mpg);
  trip.gasPrice = prefs.getFloat("gasPrice", trip.gasPrice);
  String savedVehicle = prefs.getString("vehicle", trip.vehicle);
  String savedFuel = prefs.getString("fuelType", trip.fuelType);
  setFixedText(trip.vehicle, sizeof(trip.vehicle), savedVehicle);
  setFixedText(trip.fuelType, sizeof(trip.fuelType), savedFuel);
}

void loadPhoneProfile() {
  setFixedText(profile.home, sizeof(profile.home), prefs.getString("home", profile.home));
  setFixedText(profile.work, sizeof(profile.work), prefs.getString("work", profile.work));
  setFixedText(profile.timezone, sizeof(profile.timezone), prefs.getString("tz", profile.timezone));
  setFixedText(profile.dateText, sizeof(profile.dateText), prefs.getString("date", profile.dateText));
  setFixedText(profile.alarmLabel, sizeof(profile.alarmLabel), prefs.getString("alarmLbl", profile.alarmLabel));
  setFixedText(profile.alarmTime, sizeof(profile.alarmTime), prefs.getString("alarmTime", profile.alarmTime));
  profile.alarmEnabled = prefs.getBool("alarmOn", profile.alarmEnabled);
  profile.timezoneOffsetMinutes = prefs.getLong("tzOffset", profile.timezoneOffsetMinutes);
  profile.epochAtSync = prefs.getULong("epoch", 0);
  profile.syncedAtMs = profile.epochAtSync > 0 ? millis() : 0;
  activeTheme = prefs.getUChar("themeId", activeTheme);
  clockFaceId = prefs.getUChar("clockFace", 0);
  if (clockFaceId > 5) clockFaceId = 0;
  setFixedText(phoneWeather.label, sizeof(phoneWeather.label), prefs.getString("wxLabel", phoneWeather.label));
  setFixedText(phoneWeather.city, sizeof(phoneWeather.city), prefs.getString("wxCity", phoneWeather.city));
  phoneWeather.temp = prefs.getInt("wxTemp", phoneWeather.temp);
  phoneWeather.high = prefs.getInt("wxHigh", phoneWeather.high);
  phoneWeather.low = prefs.getInt("wxLow", phoneWeather.low);
  phoneWeather.valid = prefs.getBool("wxValid", false);
  phoneWeather.updatedMs = phoneWeather.valid ? millis() : 0;
  phoneFitness.steps = prefs.getUInt("fitSteps", 0);
  phoneFitness.distanceMiles = prefs.getFloat("fitMiles", 0.0f);
  phoneFitness.calories = prefs.getUShort("fitCal", 0);
  phoneFitness.activeMinutes = prefs.getUShort("fitMin", 0);
  phoneFitness.goal = prefs.getUInt("fitGoal", 8000);
  phoneFitness.valid = prefs.getBool("fitValid", false);
  phoneFitness.updatedMs = phoneFitness.valid ? millis() : 0;
  if (!prefs.getBool("clockFaces2", false)) {
    clockFaceId = 1;
    prefs.putUChar("clockFace", clockFaceId);
    prefs.putBool("clockFaces2", true);
  }
}

int tx(int x, float scale, int ox) {
  return (int)((x - LCD_WIDTH / 2) * scale + LCD_WIDTH / 2 + ox);
}

int ty(int y, float scale, int oy) {
  return (int)((y - LCD_HEIGHT / 2) * scale + LCD_HEIGHT / 2 + oy);
}

void thickLine(int x1, int y1, int x2, int y2, uint16_t color, int width) {
  int radius = max(1, width / 2);
  bool horizontal = abs(x2 - x1) >= abs(y2 - y1);
  for (int i = -radius; i <= radius; i++) {
    if (horizontal) {
      gfx->drawLine(x1, y1 + i, x2, y2 + i, color);
    } else {
      gfx->drawLine(x1 + i, y1, x2 + i, y2, color);
    }
  }
  gfx->fillCircle(x1, y1, radius, color);
  gfx->fillCircle(x2, y2, radius, color);
}

void drawRoad(const Road &road, float scale, int ox, int oy) {
  int x1 = tx(road.x1, scale, ox);
  int y1 = ty(road.y1, scale, oy);
  int x2 = tx(road.x2, scale, ox);
  int y2 = ty(road.y2, scale, oy);
  int width = max(2, (int)(road.width * scale));
  thickLine(x1, y1, x2, y2, COL_BLACK, width + 8);
  thickLine(x1, y1, x2, y2, road.color, width);
  if (width > 8) {
    thickLine(x1, y1, x2, y2, 0x7BEF, 1);
  }
}

void drawRouteToPoint(int destX, int destY, float scale, int ox, int oy) {
  int sx = LCD_WIDTH / 2;
  int sy = LCD_HEIGHT / 2;
  int mx = tx(166, scale, ox);
  int my = ty(326, scale, oy);
  int ex = tx(destX, scale, ox);
  int ey = ty(destY, scale, oy);
  thickLine(sx, sy, mx, my, COL_BLACK, 10);
  thickLine(mx, my, ex, ey, COL_BLACK, 10);
  thickLine(sx, sy, mx, my, COL_ROUTE, 5);
  thickLine(mx, my, ex, ey, COL_ROUTE, 5);
}

void drawBlock(const Block &block, float scale, int ox, int oy) {
  int x = tx(block.x, scale, ox);
  int y = ty(block.y, scale, oy);
  int w = max(2, (int)(block.w * scale));
  int h = max(2, (int)(block.h * scale));
  gfx->fillRect(x + 2, y + 2, w, h, 0x6B4D);
  gfx->fillRect(x, y, w, h, block.color);
  gfx->drawRect(x, y, w, h, 0xC638);
}

int mapTx(int x, int mapX, int mapW, float scale, int ox) {
  return mapX + mapW / 2 + (int)((x - 205) * scale) + ox;
}

int mapTy(int y, int mapTop, int mapBottom, float scale, int oy) {
  return mapTop + (mapBottom - mapTop) / 2 + (int)((y - 251) * scale) + oy;
}

void drawSnazzyFallbackMap(int mapX, int mapTop, int mapW, int mapBottom) {
  const bool wide = !radarIsCircular();
  const float scale = wide ? 1.06f : 1.62f;
  const int ox = wide ? 0 : -8;
  const int oy = wide ? -10 : 8;

  for (uint8_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
    int x = mapTx(blocks[i].x, mapX, mapW, scale, ox);
    int y = mapTy(blocks[i].y, mapTop, mapBottom, scale, oy);
    int w = max(8, (int)(blocks[i].w * scale));
    int h = max(7, (int)(blocks[i].h * scale));
    if (x > LCD_WIDTH || y > mapBottom || x + w < 0 || y + h < mapTop) {
      continue;
    }
    gfx->fillRect(x, y, w, h, COL_MAP_BLOCK);
    gfx->drawRect(x, y, w, h, COL_BLACK);
  }

  for (uint8_t i = 0; i < sizeof(roads) / sizeof(roads[0]); i++) {
    int x1 = mapTx(roads[i].x1, mapX, mapW, scale, ox);
    int y1 = mapTy(roads[i].y1, mapTop, mapBottom, scale, oy);
    int x2 = mapTx(roads[i].x2, mapX, mapW, scale, ox);
    int y2 = mapTy(roads[i].y2, mapTop, mapBottom, scale, oy);
    int width = max(3, (int)(roads[i].width * scale * 0.82f));
    thickLine(x1, y1, x2, y2, COL_BLACK, width + 5);
    thickLine(x1, y1, x2, y2, roads[i].width >= 15 ? COL_MAP_ROAD : COL_MAP_ROAD_MINOR, width);
  }
}

void printAt(int x, int y, String text, uint16_t color, uint8_t size) {
  gfx->setCursor(x, y);
  gfx->setTextSize(size);
  gfx->setTextColor(color);
  gfx->print(text);
}

void drawSoftPanel(int x, int y, int w, int h, uint16_t fill, uint16_t edge) {
  gfx->fillRect(x, y, w, h, fill);
  gfx->drawRect(x, y, w, h, edge);
}

void fillSoftRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
  gfx->fillRect(x + r, y, w - 2 * r, h, color);
  gfx->fillRect(x, y + r, w, h - 2 * r, color);
  gfx->fillCircle(x + r, y + r, r, color);
  gfx->fillCircle(x + w - r - 1, y + r, r, color);
  gfx->fillCircle(x + r, y + h - r - 1, r, color);
  gfx->fillCircle(x + w - r - 1, y + h - r - 1, r, color);
}

void drawSoftRoundRect(int x, int y, int w, int h, int r, uint16_t color) {
  gfx->drawFastHLine(x + r, y, w - 2 * r, color);
  gfx->drawFastHLine(x + r, y + h - 1, w - 2 * r, color);
  gfx->drawFastVLine(x, y + r, h - 2 * r, color);
  gfx->drawFastVLine(x + w - 1, y + r, h - 2 * r, color);
  gfx->drawCircle(x + r, y + r, r, color);
  gfx->drawCircle(x + w - r - 1, y + r, r, color);
  gfx->drawCircle(x + r, y + h - r - 1, r, color);
  gfx->drawCircle(x + w - r - 1, y + h - r - 1, r, color);
}

void drawSettingRow(int y, const char *label, String value, bool active = false) {
  uint16_t edge = active ? COL_ROUTE : 0x4A49;
  uint16_t fill = active ? 0x3008 : COL_PANEL;
  drawSoftPanel(20, y, LCD_WIDTH - 40, 46, fill, edge);
  printAt(38, y + 8, label, active ? COL_ROUTE : COL_TEXT, 1);
  printAt(176, y + 8, fit(value, 20), active ? COL_TEXT : COL_MUTED, 1);
}

void drawPageTitle(const char *title, const char *subtitle) {
  drawTopBar();
  printAt(24, 70, title, COL_ROUTE, 2);
  if (subtitle && subtitle[0]) {
    printAt(24, 98, subtitle, COL_MUTED, 1);
  }
}

void drawStatTile(int x, int y, int w, const char *label, String value, uint16_t accent) {
  drawSoftPanel(x, y, w, 62, COL_BLACK, accent);
  printAt(x + 12, y + 10, label, accent, 1);
  printAt(x + 12, y + 32, fit(value, max(8, (w - 24) / 12)), COL_TEXT, 2);
}

void drawToggleRow(int y, const char *label, String value, bool enabled) {
  drawSoftPanel(22, y, LCD_WIDTH - 44, 52, COL_PANEL, enabled ? COL_SAFE : COL_EDGE);
  printAt(40, y + 9, label, COL_TEXT, 2);
  int pillX = LCD_WIDTH - 118;
  gfx->fillRect(pillX, y + 12, 76, 28, enabled ? COL_SAFE : COL_BLACK);
  gfx->drawRect(pillX, y + 12, 76, 28, enabled ? COL_TEXT : COL_EDGE_BRIGHT);
  printAt(pillX + 10, y + 20, value, enabled ? COL_BLACK : COL_MUTED, 1);
}

void drawTopBar() {
  gfx->fillRect(0, 0, LCD_WIDTH, 46, 0x0000);
  gfx->drawFastHLine(0, 45, LCD_WIDTH, 0x8C71);
  printAt(16, 14, bridgeTimeText(false), COL_TEXT, 2);
  printAt(132, 18, bleConnected ? "PHONE LINK" : "PAIR CHRONOS", bleConnected ? COL_SAFE : COL_ROUTE, 1);
  char notif[16];
  snprintf(notif, sizeof(notif), "%d MSG", watch.getNotificationCount());
  printAt(LCD_WIDTH - 72, 18, notif, COL_MUTED, 1);
}

Screen nextPrimaryScreen(Screen screen, int dir) {
  const Screen primary[] = {SCREEN_LAUNCHER, SCREEN_MAP, SCREEN_NOTIFICATIONS, SCREEN_PHONE, SCREEN_CLOCK, SCREEN_BUDDY, SCREEN_FITNESS, SCREEN_CALENDAR, SCREEN_SETTINGS};
  const int count = sizeof(primary) / sizeof(primary[0]);
  int index = 0;
  if (screen == SCREEN_MESSAGE_DETAIL) {
    screen = SCREEN_NOTIFICATIONS;
  } else if (screen == SCREEN_RECENTS || screen == SCREEN_GALLERY) {
    screen = SCREEN_PHONE;
  } else if (screen == SCREEN_PLACES || screen == SCREEN_MECHANIC) {
    screen = SCREEN_MAP;
  } else if (screen == SCREEN_WEATHER) {
    screen = SCREEN_CLOCK;
  } else if (screen == SCREEN_ALARMS) {
    screen = SCREEN_CALENDAR;
  } else if (screen == SCREEN_ABOUT) {
    screen = SCREEN_SETTINGS;
  } else if (screen == SCREEN_ALARMS) {
    screen = SCREEN_CALENDAR;
  }
  for (int i = 0; i < count; i++) {
    if (primary[i] == screen) {
      index = i;
      break;
    }
  }
  index = (index + dir + count) % count;
  return primary[index];
}

void openVerticalPage(int dy) {
  if (activeScreen == SCREEN_LAUNCHER) {
    activeScreen = dy < 0 ? SCREEN_MAP : SCREEN_CLOCK;
  } else if (activeScreen == SCREEN_MAP) {
    activeScreen = dy < 0 ? SCREEN_PLACES : SCREEN_MECHANIC;
  } else if (activeScreen == SCREEN_NOTIFICATIONS) {
    activeScreen = SCREEN_MESSAGE_DETAIL;
  } else if (activeScreen == SCREEN_MESSAGE_DETAIL) {
    activeScreen = SCREEN_NOTIFICATIONS;
  } else if (activeScreen == SCREEN_PHONE) {
    activeScreen = dy < 0 ? SCREEN_RECENTS : SCREEN_GALLERY;
  } else if (activeScreen == SCREEN_RECENTS) {
    activeScreen = SCREEN_PHONE;
  } else if (activeScreen == SCREEN_GALLERY) {
    activeScreen = SCREEN_PHONE;
  } else if (activeScreen == SCREEN_CLOCK) {
    activeScreen = dy < 0 ? SCREEN_WEATHER : SCREEN_CALENDAR;
  } else if (activeScreen == SCREEN_FITNESS) {
    activeScreen = dy < 0 ? SCREEN_WEATHER : SCREEN_CALENDAR;
  } else if (activeScreen == SCREEN_CALENDAR) {
    activeScreen = dy < 0 ? SCREEN_FITNESS : SCREEN_ALARMS;
  } else if (activeScreen == SCREEN_ALARMS) {
    activeScreen = SCREEN_CALENDAR;
  } else if (activeScreen == SCREEN_SETTINGS) {
    activeScreen = SCREEN_ABOUT;
  } else if (activeScreen == SCREEN_BUDDY) {
    activeScreen = dy < 0 ? SCREEN_PHONE : SCREEN_SETTINGS;
  } else if (activeScreen == SCREEN_WEATHER) {
    activeScreen = SCREEN_CLOCK;
  } else if (activeScreen == SCREEN_ABOUT) {
    activeScreen = SCREEN_SETTINGS;
  }
  needsRedraw = true;
}

void drawMessagesFooter();

void drawPlayer() {
  int cx = LCD_WIDTH / 2;
  int cy = LCD_HEIGHT / 2;
  gfx->fillTriangle(cx, cy - 28, cx + 18, cy + 20, cx, cy + 9, COL_PLAYER);
  gfx->fillTriangle(cx, cy - 28, cx, cy + 9, cx - 18, cy + 20, COL_PLAYER);
  gfx->drawTriangle(cx, cy - 28, cx + 18, cy + 20, cx - 18, cy + 20, COL_BLACK);
  gfx->fillCircle(cx, cy, 4, COL_BLACK);
}

void drawGpsPlayer() {
  drawPlayer();
  if (!liveGps.valid) {
    return;
  }
  int cx = LCD_WIDTH / 2;
  int cy = LCD_HEIGHT / 2;
  float rad = liveGps.heading * 0.017453292f;
  int hx = cx + (int)(sin(rad) * 32.0f);
  int hy = cy - (int)(cos(rad) * 32.0f);
  gfx->drawLine(cx, cy, hx, hy, COL_SAFE);
  gfx->fillCircle(hx, hy, 3, COL_SAFE);
}

void drawPlaceMarker(const Place &place, bool selected, float scale, int ox, int oy) {
  int x = tx(place.x, scale, ox);
  int y = ty(place.y, scale, oy);
  uint16_t ring = selected ? COL_ROUTE : COL_BLACK;
  gfx->fillCircle(x, y, selected ? 10 : 7, ring);
  gfx->fillCircle(x, y, selected ? 6 : 4, place.color);
  if (selected) {
    int labelW = strlen(place.label) * 6 + 10;
    int lx = constrain(x + 10, 2, LCD_WIDTH - labelW - 2);
    int ly = constrain(y - 10, 50, LCD_HEIGHT - 70);
    gfx->fillRect(lx, ly, labelW, 16, COL_BLACK);
    gfx->drawRect(lx, ly, labelW, 16, place.color);
    printAt(lx + 5, ly + 4, place.label, COL_TEXT, 1);
  }
}

void drawParkingMarker(float scale, int ox, int oy) {
  if (!parkingSaved) {
    return;
  }
  int px = liveGps.valid ? gpsToMapX(parkedGps.lon) : 322;
  int py = liveGps.valid ? gpsToMapY(parkedGps.lat) : 388;
  int x = tx(px, scale, ox);
  int y = ty(py, scale, oy);
  gfx->fillRect(x - 10, y - 7, 20, 14, COL_BLACK);
  gfx->fillRect(x - 7, y - 11, 14, 7, COL_BLACK);
  gfx->fillRect(x - 8, y - 5, 16, 8, 0x66FF);
  gfx->fillCircle(x - 5, y + 5, 3, COL_TEXT);
  gfx->fillCircle(x + 5, y + 5, 3, COL_TEXT);
  if (routeToParking) {
    gfx->fillRect(constrain(x + 12, 4, LCD_WIDTH - 88), constrain(y - 12, 52, LCD_HEIGHT - 72), 80, 16, COL_BLACK);
    printAt(constrain(x + 17, 9, LCD_WIDTH - 83), constrain(y - 8, 56, LCD_HEIGHT - 68), "Parked Car", COL_TEXT, 1);
  }
}

void drawNavigationIcon(int x, int y, uint16_t color) {
  Navigation &nav = watch.getNavigation();
  if (!nav.hasIcon) {
    gfx->fillTriangle(x + 24, y + 4, x + 42, y + 44, x + 24, y + 34, color);
    gfx->fillTriangle(x + 24, y + 4, x + 24, y + 34, x + 6, y + 44, color);
    return;
  }

  for (int py = 0; py < 48; py++) {
    for (int px = 0; px < 48; px++) {
      int byteIndex = (py * 48 + px) / 8;
      int bitPos = 7 - (px % 8);
      if ((nav.icon[byteIndex] >> bitPos) & 0x01) {
        gfx->drawPixel(x + px, y + py, color);
      }
    }
  }
}

void drawHudPanel() {
  Navigation &nav = watch.getNavigation();
  int y = LCD_HEIGHT - 118;
  gfx->fillRect(0, y, LCD_WIDTH, 118, 0x0000);
  gfx->drawFastHLine(0, y, LCD_WIDTH, COL_ROUTE);

  if (nav.active) {
    drawNavigationIcon(18, y + 16, COL_ROUTE);
    printAt(78, y + 12, fit(rewritePlaceTerms(nav.title), 24), COL_TEXT, 2);
    printAt(78, y + 38, fit(rewritePlaceTerms(nav.directions), 36), COL_MUTED, 1);
    String meta = fit(nav.distance + "  " + nav.duration + "  ETA " + nav.eta, 42);
    printAt(78, y + 58, meta, COL_ROUTE, 1);
    if (nav.speed.length()) {
      printAt(LCD_WIDTH - 76, y + 74, nav.speed, COL_SAFE, 1);
    }
  } else {
    const char *mode = trip.driving ? "DRIVE" : "WALK";
    printAt(18, y + 10, fit(routeToParking ? "PARKED CAR" : trip.destination, 22), COL_TEXT, 2);
    printAt(18, y + 36, mode, trip.driving ? COL_ROUTE : COL_SAFE, 2);
    printAt(90, y + 41, String(trip.routeMiles, 1) + " mi", COL_MUTED, 1);
    printAt(166, y + 41, trip.driving ? String(trip.fuelType) : "No fuel", COL_MUTED, 1);
    printAt(18, y + 62, "Gas " + dollars(fuelCost()), COL_TEXT, 1);
    printAt(116, y + 62, "Toll " + dollars(trip.driving ? trip.tollCost : 0.0f), COL_TEXT, 1);
    printAt(226, y + 62, "Total " + dollars(tripCost()), COL_ROUTE, 1);
    printAt(18, y + 84, fit(liveGps.valid ? "Live GPS bridge active" : "GPS waiting for phone bridge", 38), liveGps.valid ? COL_SAFE : COL_MUTED, 1);
  }
}

float metersPerLonAtLewiston() {
  return 111320.0f * cos(LEWISTON_CENTER_LAT * 0.017453292f);
}

float gpsEastMeters() {
  if (!liveGps.valid || !gpsAnchorValid) {
    return 0.0f;
  }
  return (liveGps.lon - gpsAnchorLon) * metersPerLonAtLewiston();
}

float gpsSouthMeters() {
  if (!liveGps.valid || !gpsAnchorValid) {
    return 0.0f;
  }
  return (gpsAnchorLat - liveGps.lat) * 111320.0f;
}

float lonToLocalMeters(float lon) {
  return (lon - LEWISTON_CENTER_LON) * metersPerLonAtLewiston();
}

float latToLocalMeters(float lat) {
  return (LEWISTON_CENTER_LAT - lat) * 111320.0f;
}

uint16_t osmRoadColor(uint8_t weight) {
  if (weight >= 6) {
    return COL_MAP_ROAD;
  }
  if (weight >= 4) {
    return COL_MAP_ROAD_MINOR;
  }
  if (weight >= 3) {
    return COL_MAP_ROAD_LOCAL;
  }
  return COL_MAP_ROAD_LOCAL;
}

uint8_t osmRoadWidth(uint8_t weight, bool overview) {
  if (overview) {
    return weight >= 6 ? 5 : (weight >= 4 ? 4 : 2);
  }
  if (weight >= 6) {
    return 11;
  }
  if (weight >= 4) {
    return 8;
  }
  if (weight >= 3) {
    return 5;
  }
  return 3;
}

bool drawOsmRoadSegment(const OsmRoadSegment &seg, float centerX, float centerY,
                        float scale, int cx, int cy, int clipLeft, int clipRight,
                        int clipTop, int clipBottom, bool overview) {
  int x1 = cx + (int)((seg.x1 - centerX) * scale);
  int y1 = cy + (int)((seg.y1 - centerY) * scale);
  int x2 = cx + (int)((seg.x2 - centerX) * scale);
  int y2 = cy + (int)((seg.y2 - centerY) * scale);
  int margin = 18;
  if ((x1 < clipLeft - margin && x2 < clipLeft - margin) || (x1 > clipRight + margin && x2 > clipRight + margin) ||
      (y1 < clipTop - margin && y2 < clipTop - margin) || (y1 > clipBottom + margin && y2 > clipBottom + margin)) {
    return false;
  }

  uint8_t width = osmRoadWidth(seg.weight, overview);
  uint16_t color = osmRoadColor(seg.weight);
  if (width <= 1) {
    gfx->drawLine(x1, y1, x2, y2, color);
  } else {
    thickLine(x1, y1, x2, y2, COL_MAP_OUTLINE, width + (overview ? 3 : 4));
    thickLine(x1, y1, x2, y2, color, width);
  }
  return true;
}

uint16_t osmAreaColor(uint8_t kind) {
  if (kind == 1) {
    return COL_MAP_WATER;
  }
  return COL_MAP_PARK;
}

void projectOsmAreaPoint(const OsmAreaPoint &pt, float centerX, float centerY,
                         float scale, int cx, int cy, int &x, int &y) {
  x = cx + (int)(((float)pt.x - centerX) * scale);
  y = cy + (int)(((float)pt.y - centerY) * scale);
}

void drawOsmAreaLayers(float centerX, float centerY, float scale, int cx, int cy,
                       int clipLeft, int clipRight, int clipTop, int clipBottom) {
  for (uint16_t i = 0; i < LEWISTON_OSM_AREA_FEATURE_COUNT; i++) {
    const OsmAreaFeature &feature = LEWISTON_OSM_AREAS[i];
    if (feature.count < 3) {
      continue;
    }

    int x0 = 0, y0 = 0;
    projectOsmAreaPoint(LEWISTON_OSM_AREA_POINTS[feature.start], centerX, centerY, scale, cx, cy, x0, y0);
    bool maybeVisible = x0 >= clipLeft - 40 && x0 <= clipRight + 40 &&
                        y0 >= clipTop - 40 && y0 <= clipBottom + 40;
    for (uint8_t j = 1; j < feature.count && !maybeVisible; j++) {
      int px = 0, py = 0;
      projectOsmAreaPoint(LEWISTON_OSM_AREA_POINTS[feature.start + j], centerX, centerY, scale, cx, cy, px, py);
      maybeVisible = px >= clipLeft - 40 && px <= clipRight + 40 &&
                     py >= clipTop - 40 && py <= clipBottom + 40;
    }
    if (!maybeVisible) {
      continue;
    }

    uint16_t color = osmAreaColor(feature.kind);
    for (uint8_t j = 1; j + 1 < feature.count; j++) {
      int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
      projectOsmAreaPoint(LEWISTON_OSM_AREA_POINTS[feature.start + j], centerX, centerY, scale, cx, cy, x1, y1);
      projectOsmAreaPoint(LEWISTON_OSM_AREA_POINTS[feature.start + j + 1], centerX, centerY, scale, cx, cy, x2, y2);
      gfx->fillTriangle(x0, y0, x1, y1, x2, y2, color);
    }
    for (uint8_t j = 0; j < feature.count; j++) {
      int x1 = 0, y1 = 0, x2 = 0, y2 = 0;
      const OsmAreaPoint &a = LEWISTON_OSM_AREA_POINTS[feature.start + j];
      const OsmAreaPoint &b = LEWISTON_OSM_AREA_POINTS[feature.start + ((j + 1) % feature.count)];
      projectOsmAreaPoint(a, centerX, centerY, scale, cx, cy, x1, y1);
      projectOsmAreaPoint(b, centerX, centerY, scale, cx, cy, x2, y2);
      gfx->drawLine(x1, y1, x2, y2, COL_MAP_OUTLINE);
    }
  }
}

bool drawLewistonOsmMap(int mapX, int mapTop, int mapW, int mapH) {
  int mapBottom = mapTop + mapH;
  int mapRight = mapX + mapW;
  gfx->fillRect(mapX, mapTop, mapW, mapH, radarIsCircular() ? COL_BLACK : COL_MAP_TAN);

  float centerLat = liveGps.valid ? liveGps.lat : LEWISTON_CENTER_LAT;
  float centerLon = liveGps.valid ? liveGps.lon : LEWISTON_CENTER_LON;
  float centerX = lonToLocalMeters(centerLon);
  float centerY = latToLocalMeters(centerLat);
  bool liveOutsidePack = liveGps.valid && (abs(centerX) > 1850.0f || abs(centerY) > 1850.0f);
  if (!liveGps.valid && (abs(centerX) > 1850.0f || abs(centerY) > 1850.0f)) {
    centerX = 0.0f;
    centerY = 0.0f;
  }
  if (liveOutsidePack) {
    centerX = 0.0f;
    centerY = 0.0f;
  }
  float scale = mapPixelsPerMeter();
  int cx = mapX + mapW / 2;
  int cy = mapTop + (mapBottom - mapTop) / 2;
  uint16_t visibleRoads = 0;

  if (!radarIsCircular()) {
    drawOsmAreaLayers(centerX, centerY, scale, cx, cy, mapX, mapRight, mapTop, mapBottom);
  } else {
    drawRadarWaterPatches(mapX, mapTop, mapW, mapH);
    drawRadarManMadePatches(mapX, mapTop, mapW, mapH);
  }

  for (uint16_t i = 0; i < LEWISTON_OSM_ROAD_COUNT; i++) {
    if (drawOsmRoadSegment(LEWISTON_OSM_ROADS[i], centerX, centerY, scale, cx, cy,
                           mapX, mapRight, mapTop, mapBottom, overviewMode)) {
      visibleRoads++;
    }
    if (i > 940) {
      break;
    }
    if ((i & 0x7F) == 0) {
      yield();
    }
  }

  if (!radarIsCircular()) {
    for (int x = mapX; x < mapRight; x += 82) {
      gfx->drawFastVLine(x, mapTop, mapH, 0x0841);
    }
    for (int y = mapTop; y < mapBottom; y += 68) {
      gfx->drawFastHLine(mapX, y, mapW, 0x0841);
    }
  }

  uint16_t minRoadsForMap = overviewMode ? 22 : 8;
  if (visibleRoads < minRoadsForMap) {
    gfx->fillRect(mapX, mapTop, mapW, mapH, COL_BLACK);
    drawSnazzyFallbackMap(mapX, mapTop, mapW, mapBottom);
  }

  if (overviewMode) {
    gfx->fillCircle(mapX + 17, mapTop + 18, 14, COL_BLACK);
    printAt(mapX + 12, mapTop + 13, "N", COL_TEXT, 1);
  }
  return visibleRoads >= minRoadsForMap;
}

void drawMapBitmapChunked(const uint16_t *bitmap, int width, int height, int x, int y) {
  const int chunkRows = 24;
  for (int row = 0; row < height; row += chunkRows) {
    int rows = min(chunkRows, height - row);
    gfx->draw16bitRGBBitmap(x, y + row, bitmap + (row * width), width, rows);
    yield();
  }
}

void drawMapBitmapPatch(int x, int y, int w, int h) {
  const int mapY = 62;
  const uint16_t *bitmap = GTA_MAP_WATCH_CLOSE;
  const int bitmapW = GTA_MAP_WATCH_CLOSE_W;
  const int bitmapH = GTA_MAP_WATCH_CLOSE_H;

  int sx = constrain(x, 0, LCD_WIDTH - 1);
  int sy = constrain(y, mapY, mapY + bitmapH - 1);
  int ex = constrain(x + w, 0, LCD_WIDTH);
  int ey = constrain(y + h, mapY, mapY + bitmapH);
  if (ex <= sx || ey <= sy) {
    return;
  }

  int patchW = ex - sx;
  for (int row = sy; row < ey; row++) {
    int by = row - mapY;
    for (int col = 0; col < patchW; col++) {
      mapLine[col] = pgm_read_word(&bitmap[by * bitmapW + sx + col]);
    }
    gfx->draw16bitRGBBitmap(sx, row, mapLine, patchW, 1);
  }
}

void drawLiveGtaBitmapMap(int mapTop, int mapBottom) {
  int mapH = mapBottom - mapTop;
  float srcW = 74.0f;
  float srcH = 62.0f;
  float metersPerSourcePixel = 1.8f;
  float centerX = 119.5f;
  float centerY = 119.5f;

  if (liveGps.valid && gpsAnchorValid) {
    float eastMeters = (liveGps.lon - gpsAnchorLon) * metersPerLonAtLewiston();
    float southMeters = (gpsAnchorLat - liveGps.lat) * 111320.0f;
    centerX += eastMeters / metersPerSourcePixel;
    centerY += southMeters / metersPerSourcePixel;
  }

  float minX = srcW / 2.0f;
  float maxX = (float)GTA_MAP_SOURCE_W - srcW / 2.0f - 1.0f;
  float minY = srcH / 2.0f;
  float maxY = (float)GTA_MAP_SOURCE_H - srcH / 2.0f - 1.0f;
  centerX = constrain(centerX, minX, maxX);
  centerY = constrain(centerY, minY, maxY);

  float left = centerX - srcW / 2.0f;
  float top = centerY - srcH / 2.0f;
  for (int y = 0; y < mapH; y++) {
    int sy = constrain((int)(top + ((float)y + 0.5f) * srcH / (float)mapH), 0, GTA_MAP_SOURCE_H - 1);
    for (int x = 0; x < LCD_WIDTH; x++) {
      int sx = constrain((int)(left + ((float)x + 0.5f) * srcW / (float)LCD_WIDTH), 0, GTA_MAP_SOURCE_W - 1);
      mapLine[x] = pgm_read_word(&GTA_MAP_SOURCE[sy * GTA_MAP_SOURCE_W + sx]);
    }
    gfx->draw16bitRGBBitmap(0, mapTop + y, mapLine, LCD_WIDTH, 1);
    if ((y & 0x1F) == 0) {
      yield();
    }
  }
}

uint16_t colorForIcon(const char *icon) {
  String value(icon);
  value.toLowerCase();
  if (value.indexOf("safe") >= 0) return COL_SAFE;
  if (value.indexOf("spray") >= 0 || value.indexOf("garage") >= 0 || value.indexOf("mod") >= 0) return 0x66FF;
  if (value.indexOf("bank") >= 0) return 0xFEE0;
  if (value.indexOf("ammo") >= 0 || value.indexOf("police") >= 0 || value.indexOf("hospital") >= 0) return COL_RED;
  if (value.indexOf("cluck") >= 0 || value.indexOf("food") >= 0 || value.indexOf("burger") >= 0) return COL_ROUTE;
  if (value.indexOf("park") >= 0 || value.indexOf("mechanic") >= 0) return 0x7E7F;
  return COL_TEXT;
}

bool gpsPointToMap(int mapTop, int mapBottom, float lat, float lon, int &x, int &y) {
  if (!gpsAnchorValid && !liveGps.valid) {
    return false;
  }
  int mapX = 0, mapY = 0, mapW = LCD_WIDTH, mapH = mapBottom - mapTop;
  currentRadarRect(mapX, mapY, mapW, mapH);
  mapTop = mapY;
  mapBottom = mapY + mapH;
  float anchorLat = liveGps.valid ? liveGps.lat : gpsAnchorLat;
  float anchorLon = liveGps.valid ? liveGps.lon : gpsAnchorLon;
  float pixelsPerMeter = mapPixelsPerMeter();
  float metersPerLon = 111320.0f * cos(anchorLat * 0.017453292f);
  float east = (lon - anchorLon) * metersPerLon;
  float south = (anchorLat - lat) * 111320.0f;
  x = mapX + mapW / 2 + (int)(east * pixelsPerMeter);
  y = mapTop + (mapBottom - mapTop) / 2 + (int)(south * pixelsPerMeter);
  return x >= mapX && x < mapX + mapW && y >= mapTop && y < mapBottom;
}

void drawCustomPlaces(int mapTop, int mapBottom) {
  for (int i = 0; i < CUSTOM_PLACE_COUNT; i++) {
    if (!customPlaces[i].valid) {
      continue;
    }
    int x = 0;
    int y = 0;
    if (!gpsPointToMap(mapTop, mapBottom, customPlaces[i].lat, customPlaces[i].lon, x, y)) {
      continue;
    }
    uint16_t color = customPlaces[i].color;
    gfx->fillCircle(x, y, 9, COL_BLACK);
    gfx->fillCircle(x, y, 6, color);
    gfx->drawCircle(x, y, 10, COL_TEXT);
    if (false) {
      int labelW = strlen(customPlaces[i].label) * 6 + 8;
      int lx = constrain(x + 10, 2, LCD_WIDTH - labelW - 2);
      int ly = constrain(y - 8, mapTop + 2, mapBottom - 18);
      gfx->fillRect(lx, ly, labelW, 15, COL_BLACK);
      gfx->drawRect(lx, ly, labelW, 15, color);
      printAt(lx + 4, ly + 4, fit(String(customPlaces[i].label), 14), COL_TEXT, 1);
    }
  }
}

void drawDestinationRoute(int mapTop, int mapBottom) {
  if (!liveGps.valid || !destinationGps.valid) {
    return;
  }
  int mapX = 0, mapY = 0, mapW = LCD_WIDTH, mapH = mapBottom - mapTop;
  currentRadarRect(mapX, mapY, mapW, mapH);
  mapTop = mapY;
  mapBottom = mapY + mapH;
  int x = 0;
  int y = 0;
  gpsPointToMap(mapTop, mapBottom, destinationGps.lat, destinationGps.lon, x, y);
  x = constrain(x, mapX + 18, mapX + mapW - 18);
  y = constrain(y, mapTop + 18, mapBottom - 18);

  const int cx = mapX + mapW / 2;
  const int cy = mapTop + (mapBottom - mapTop) / 2;
  thickLine(cx, cy, x, y, COL_MAP_OUTLINE, 9);
  thickLine(cx, cy, x, y, COL_ROUTE, 4);
  gfx->fillCircle(x, y, 11, COL_BLACK);
  gfx->fillCircle(x, y, 7, COL_ROUTE);
  gfx->drawCircle(x, y, 12, COL_TEXT);
}

void drawMovingGpsBlip(int mapTop, int mapBottom) {
  int mapX = 0, mapY = 0, mapW = LCD_WIDTH, mapH = mapBottom - mapTop;
  currentRadarRect(mapX, mapY, mapW, mapH);
  mapTop = mapY;
  mapBottom = mapY + mapH;
  int cx = mapX + mapW / 2;
  int cy = mapTop + (mapBottom - mapTop) / 2;
  int x = cx;
  int y = cy;

  x = constrain(x, mapX + 24, mapX + mapW - 24);
  y = constrain(y, mapTop + 24, mapBottom - 24);

  float rad = liveGps.heading * 0.017453292f;
  float tipLen = radarIsCircular() ? 16.0f : 26.0f;
  float wingLen = radarIsCircular() ? 10.0f : 17.0f;
  float tailLen = radarIsCircular() ? 6.0f : 8.0f;
  int tipX = x + (int)(sin(rad) * tipLen);
  int tipY = y - (int)(cos(rad) * tipLen);
  int leftX = x + (int)(sin(rad + 2.45f) * wingLen);
  int leftY = y - (int)(cos(rad + 2.45f) * wingLen);
  int rightX = x + (int)(sin(rad - 2.45f) * wingLen);
  int rightY = y - (int)(cos(rad - 2.45f) * wingLen);
  int tailX = x - (int)(sin(rad) * tailLen);
  int tailY = y + (int)(cos(rad) * tailLen);

  if (!radarIsCircular()) {
    uint16_t ring = gpsFresh() ? COL_SAFE : COL_ROUTE;
    gfx->fillCircle(x, y, 23, COL_BLACK);
    gfx->drawCircle(x, y, 21, ring);
    gfx->drawCircle(x, y, 18, COL_BLACK);
  }
  gfx->fillTriangle(tipX, tipY, leftX, leftY, tailX, tailY, COL_BLACK);
  gfx->fillTriangle(tipX, tipY, tailX, tailY, rightX, rightY, COL_BLACK);
  gfx->fillTriangle(tipX, tipY, leftX, leftY, tailX, tailY, COL_PLAYER);
  gfx->fillTriangle(tipX, tipY, tailX, tailY, rightX, rightY, COL_PLAYER);
  gfx->drawLine(tipX, tipY, leftX, leftY, COL_BLACK);
  gfx->drawLine(tipX, tipY, rightX, rightY, COL_BLACK);
  gfx->drawLine(leftX, leftY, tailX, tailY, COL_BLACK);
  gfx->drawLine(rightX, rightY, tailX, tailY, COL_BLACK);
  if (!radarIsCircular()) {
    gfx->fillCircle(x, y, 5, COL_BLACK);
    gfx->fillCircle(x, y, 2, gpsFresh() ? COL_SAFE : COL_ROUTE);
  }
  mapBlipLastX = x - 34;
  mapBlipLastY = y - 34;
  mapBlipLastW = 68;
  mapBlipLastH = 68;
  mapBlipRectValid = true;
}

void drawMapLiveOverlay() {
  if (activeScreen != SCREEN_MAP) {
    return;
  }
  if (overviewMode) {
    drawMapScreen();
    return;
  }
  if (!liveGps.valid) {
    needsRedraw = true;
    return;
  }
  int mapX = 0, mapY = 0, mapW = 0, mapH = 0;
  currentRadarRect(mapX, mapY, mapW, mapH);
  const int panelY = mapY + mapH;
  drawLewistonOsmMap(mapX, mapY, mapW, mapH);
  drawDestinationRoute(mapY, panelY);
  drawCustomPlaces(mapY, panelY);
  drawMovingGpsBlip(mapY, panelY);
  maskCircularRadarOutside(mapX, mapY, mapW, mapH);
  drawRadarFrame(mapX, mapY, mapW, mapH);
  if (overviewMode) {
    gfx->fillRect(LCD_WIDTH - 84, 38, 78, 18, COL_BLACK);
    printAt(LCD_WIDTH - 80, 42, "H " + String((int)liveGps.heading), COL_SAFE, 1);
  }
}

void drawWideBlock(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, uint16_t fill, uint16_t edge) {
  gfx->fillTriangle(x1, y1, x2, y2, x3, y3, fill);
  gfx->fillTriangle(x1, y1, x3, y3, x4, y4, fill);
  gfx->drawLine(x1, y1, x2, y2, edge);
  gfx->drawLine(x2, y2, x3, y3, edge);
  gfx->drawLine(x3, y3, x4, y4, edge);
  gfx->drawLine(x4, y4, x1, y1, edge);
}

void drawWideRoad(int x1, int y1, int x2, int y2, int width, uint16_t road) {
  thickLine(x1, y1, x2, y2, COL_BLACK, width + 7);
  thickLine(x1, y1, x2, y2, road, width);
  thickLine(x1, y1, x2, y2, 0xBDF7, 2);
}

void drawWideRouteLine(int x1, int y1, int x2, int y2) {
  thickLine(x1, y1, x2, y2, COL_BLACK, 16);
  thickLine(x1, y1, x2, y2, 0x8A7F, 9);
  thickLine(x1, y1, x2, y2, 0xB35F, 5);
}

void drawWidePlayerBlip(int cx, int cy) {
  gfx->fillTriangle(cx, cy - 18, cx - 11, cy + 15, cx, cy + 8, COL_PLAYER);
  gfx->fillTriangle(cx, cy - 18, cx, cy + 8, cx + 11, cy + 15, COL_PLAYER);
  gfx->drawTriangle(cx, cy - 20, cx - 13, cy + 17, cx + 13, cy + 17, COL_BLACK);
  gfx->drawLine(cx, cy - 17, cx, cy + 9, COL_BLACK);
}

void drawWideMapHud() {
  gfx->fillScreen(COL_BLACK);
  mapBlipRectValid = false;

  const int top = 46;
  const int bottom = LCD_HEIGHT - 30;
  gfx->fillRect(0, top, LCD_WIDTH, bottom - top, COL_BLACK);
  drawMapBitmapChunked(GTA_MAP_WATCH_CLOSE, GTA_MAP_WATCH_CLOSE_W, GTA_MAP_WATCH_CLOSE_H, 0, top);
  if (bottom > top + GTA_MAP_WATCH_CLOSE_H) {
    gfx->fillRect(0, top + GTA_MAP_WATCH_CLOSE_H, LCD_WIDTH, bottom - top - GTA_MAP_WATCH_CLOSE_H, COL_BLACK);
  }

  if (destinationGps.valid || routeToParking || strlen(trip.destination) > 0) {
    drawWideRouteLine(204, bottom - 74, 206, 258);
    drawWideRouteLine(206, 258, 334, 190);
    drawWideRouteLine(334, 190, 380, 92);
  } else {
    drawWideRouteLine(204, bottom - 74, 206, 258);
    drawWideRouteLine(206, 258, 300, 212);
  }

  gfx->fillCircle(24, bottom - 196, 15, COL_BLACK);
  printAt(17, bottom - 201, "N", COL_TEXT, 1);
  printAt(42, bottom - 128, liveGps.valid ? "1.04km" : "-- km", COL_TEXT, 2);

  drawWidePlayerBlip(LCD_WIDTH / 2, bottom - 78);

  gfx->fillRect(0, LCD_HEIGHT - 34, LCD_WIDTH, 34, COL_BLACK);
  gfx->fillRect(8, LCD_HEIGHT - 30, 30, 26, 0x5AEB);
  gfx->drawRect(8, LCD_HEIGHT - 30, 30, 26, COL_BLACK);
  printAt(15, LCD_HEIGHT - 24, "M", 0x4D9F, 2);
  int batteryPct = constrain(watch.getPhoneBattery(), 0, 100);
  float remainMiles = routeMilesRemaining();
  int routePct = constrain((int)(remainMiles > 0.05f ? (100.0f - min(remainMiles, 10.0f) * 10.0f) : 100.0f), 0, 100);
  gfx->fillRect(46, LCD_HEIGHT - 24, 240, 12, 0x2965);
  gfx->fillRect(48, LCD_HEIGHT - 22, batteryPct * 116 / 100, 8, COL_SAFE);
  gfx->fillRect(166, LCD_HEIGHT - 22, routePct * 118 / 100, 8, 0x4D9F);
  printAt(48, LCD_HEIGHT - 40, String(batteryPct) + "%", COL_TEXT, 1);
  printAt(166, LCD_HEIGHT - 40, remainMiles > 0.05f ? String(remainMiles, 1) + "mi" : "ARRIVE", COL_TEXT, 1);
  printAt(312, LCD_HEIGHT - 28, "?", 0x4D9F, 2);
  gfx->fillCircle(384, LCD_HEIGHT - 18, 13, 0x8A7F);
  gfx->drawFastHLine(371, LCD_HEIGHT - 18, 26, COL_BLACK);
  gfx->drawFastVLine(384, LCD_HEIGHT - 31, 26, COL_BLACK);

  gfx->fillRect(0, 0, LCD_WIDTH, top, COL_BLACK);
  printAt(12, 14, gpsFresh() ? "PHONE GPS LIVE" : (liveGps.valid ? "GPS STALE" : "GPS WAITING"),
          gpsFresh() ? COL_SAFE : COL_ROUTE, 1);
  printAt(262, 14, "DOUBLE TAP CLOSE", COL_MUTED, 1);
}

void drawMapBlipOverlay() {
  if (activeScreen != SCREEN_MAP) {
    return;
  }
  drawMapLiveOverlay();
}

bool ftRead8(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(FT3168_DEVICE_ADDRESS);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  if (Wire.requestFrom((uint8_t)FT3168_DEVICE_ADDRESS, (uint8_t)1) != 1) {
    return false;
  }
  value = Wire.read();
  return true;
}

bool ftReadTouchRaw(int &x, int &y, int &fingers) {
  uint8_t f = 0, xh = 0, xl = 0, yh = 0, yl = 0;
  bool ok = ftRead8(0x02, f) && ftRead8(0x03, xh) && ftRead8(0x04, xl) &&
            ftRead8(0x05, yh) && ftRead8(0x06, yl);
  if (!ok) {
    fingers = -1;
    x = -1;
    y = -1;
    return false;
  }
  fingers = f & 0x0F;
  x = ((xh & 0x0F) << 8) | xl;
  y = ((yh & 0x0F) << 8) | yl;
  return true;
}

void drawMapScreen() {
  gfx->fillScreen(COL_BLACK);
  mapBlipRectValid = false;

  if (overviewMode) {
    drawWideMapHud();
    return;
  }

  int mapX = 0, mapY = 0, mapW = 0, mapH = 0;
  currentRadarRect(mapX, mapY, mapW, mapH);
  int panelY = mapY + mapH;
  drawLiveGtaBitmapMap(mapY, panelY);

  drawDestinationRoute(mapY, panelY);
  drawCustomPlaces(mapY, panelY);
  drawMovingGpsBlip(mapY, panelY);
  maskCircularRadarOutside(mapX, mapY, mapW, mapH);
  drawRadarFrame(mapX, mapY, mapW, mapH);

  if (overviewMode) {
    gfx->fillRect(0, 0, LCD_WIDTH, mapY, COL_BLACK);
    printAt(14, 14, gpsFresh() ? "PHONE GPS LIVE" : (liveGps.valid ? "GPS STALE" : "GPS WAITING"),
            gpsFresh() ? COL_SAFE : COL_ROUTE, 1);
    if (millis() - lastHeadCommandMs < 4000) {
      printAt(LCD_WIDTH - 80, 42, "H " + String((int)liveGps.heading), COL_SAFE, 1);
    } else {
      printAt(LCD_WIDTH - 118, 42, "TAP CLOSE", COL_MUTED, 1);
    }
    gfx->fillRect(0, panelY, LCD_WIDTH, LCD_HEIGHT - panelY, COL_BLACK);
    gfx->drawFastHLine(0, panelY, LCD_WIDTH, COL_ROUTE);
    printAt(16, panelY + 14, fit(routeToParking ? "Route: parked car" : String("Route: ") + trip.destination, 34), COL_TEXT, 1);
    printAt(16, panelY + 40, String(trip.routeMiles, 1) + " mi  " + (trip.driving ? String(trip.fuelType) : "Walk"), COL_MUTED, 1);
    printAt(16, panelY + 66, liveGps.valid ? "Wide OSM mini  |  double tap radar" : "Waiting for phone GPS", COL_MUTED, 1);
  }
}

void currentClockParts(int &hour, int &minute, int &second) {
  int year, month, day;
  if (phoneTimeValid()) {
    datePartsFromEpoch(phoneEpochNow(), year, month, day, hour, minute, second);
    return;
  }
  hour = watch.getTime("%H").toInt();
  minute = watch.getTime("%M").toInt();
  second = watch.getTime("%S").toInt();
}

void drawClockFooter() {
  Navigation &nav = watch.getNavigation();
  if (nav.active) {
    printAt(32, 398, fit(rewritePlaceTerms(nav.title), 34), COL_TEXT, 1);
    printAt(32, 422, fit(rewritePlaceTerms(nav.distance + "  " + nav.duration), 34), COL_ROUTE, 1);
  } else {
    printAt(32, 398, "No route running", COL_MUTED, 1);
    printAt(32, 422, String("Saved: ") + places[selectedPlace].label, COL_ROUTE, 1);
  }
  static const char *faces[] = {"DIGI", "ANLG", "CLSC", "ONE", "BASIC", "LUX"};
  for (int i = 0; i < 6; i++) {
    int x = 24 + i * 62;
    uint16_t fill = i == clockFaceId ? COL_ROUTE : COL_BLACK;
    uint16_t edge = i == clockFaceId ? COL_TEXT : COL_EDGE_BRIGHT;
    gfx->fillRect(x, 452, 54, 26, fill);
    gfx->drawRect(x, 452, 54, 26, edge);
    printAt(x + 7, 460, faces[i], i == clockFaceId ? COL_BLACK : COL_MUTED, 1);
  }
}

void drawClockStats() {
  drawStatTile(22, 300, 112, "BAT", String(watch.getPhoneBattery()) + "%", bleConnected ? COL_SAFE : COL_ROUTE);
  drawStatTile(150, 300, 112, "STEPS", effectiveSteps() > 0 ? String(effectiveSteps()) : "--", qmiReady || phoneFitness.valid ? COL_SAFE : COL_ROUTE);
  drawStatTile(278, 300, 110, "GPS", gpsFresh() ? "LIVE" : "WAIT", gpsFresh() ? COL_SAFE : COL_RED);

  char line[64];
  snprintf(line, sizeof(line), "%s  %s", bleConnected ? "Phone linked" : "Phone waiting",
           phoneWeather.valid ? phoneWeather.label : "waiting");
  printAt(32, 374, line, bleConnected ? COL_SAFE : COL_ROUTE, 1);
}

void drawAnalogClockFace(uint8_t face) {
  int hour, minute, second;
  currentClockParts(hour, minute, second);
  int cx = LCD_WIDTH / 2;
  int cy = 202;
  int r = face == 5 ? 92 : 84;
  uint16_t accent = face == 5 ? 0xFEA0 : (face == 2 ? COL_TEXT : COL_ROUTE);
  drawSoftPanel(28, 104, LCD_WIDTH - 56, 184, COL_BLACK, accent);
  gfx->drawCircle(cx, cy, r, accent);
  gfx->drawCircle(cx, cy, r - 4, face == 5 ? 0xA540 : COL_MUTED);
  for (int i = 0; i < 60; i += 5) {
    float a = (i / 60.0f) * 2.0f * PI - PI / 2.0f;
    int x1 = cx + cos(a) * (r - 10);
    int y1 = cy + sin(a) * (r - 10);
    int x2 = cx + cos(a) * (r - (i % 15 == 0 ? 24 : 18));
    int y2 = cy + sin(a) * (r - (i % 15 == 0 ? 24 : 18));
    gfx->drawLine(x1, y1, x2, y2, i % 15 == 0 ? accent : COL_MUTED);
  }
  if (face == 2) {
    printAt(cx - 10, cy - r + 30, "XII", COL_TEXT, 1);
    printAt(cx + r - 34, cy - 6, "III", COL_TEXT, 1);
    printAt(cx - 8, cy + r - 44, "VI", COL_TEXT, 1);
    printAt(cx - r + 18, cy - 6, "IX", COL_TEXT, 1);
  }
  float minuteAngle = (minute / 60.0f) * 2.0f * PI - PI / 2.0f;
  float hourAngle = (((hour % 12) + minute / 60.0f) / 12.0f) * 2.0f * PI - PI / 2.0f;
  gfx->drawLine(cx, cy, cx + cos(hourAngle) * (r * 0.48f), cy + sin(hourAngle) * (r * 0.48f), COL_TEXT);
  gfx->drawLine(cx + 1, cy, cx + 1 + cos(hourAngle) * (r * 0.48f), cy + sin(hourAngle) * (r * 0.48f), COL_TEXT);
  if (face != 3) {
    gfx->drawLine(cx, cy, cx + cos(minuteAngle) * (r * 0.72f), cy + sin(minuteAngle) * (r * 0.72f), accent);
  }
  if (face == 1 || face == 5) {
    float secondAngle = (second / 60.0f) * 2.0f * PI - PI / 2.0f;
    gfx->drawLine(cx, cy, cx + cos(secondAngle) * (r * 0.78f), cy + sin(secondAngle) * (r * 0.78f), COL_RED);
  }
  gfx->fillCircle(cx, cy, 5, accent);
  printAt(42, 260, bridgeTimeText(false), COL_TEXT, 2);
  printAt(244, 260, face == 5 ? "LUX" : (face == 3 ? "ONE" : "ANALOG"), accent, 2);
}

void drawBasicClockFace() {
  drawSoftPanel(20, 124, LCD_WIDTH - 40, 154, COL_BLACK, COL_ROUTE);
  printAt(42, 146, bridgeTimeText(false), COL_TEXT, 5);
  printAt(44, 222, fit(bridgeDateText(), 18), COL_ROUTE, 3);
  printAt(214, 226, bridgeDowText(), COL_MUTED, 2);
}

void drawDigitalClockFace() {
  drawSoftPanel(20, 124, LCD_WIDTH - 40, 154, COL_BLACK, COL_ROUTE);
  printAt(42, 148, bridgeTimeText(false), COL_TEXT, 5);
  if (phoneTimeValid()) {
    int year, month, day, hour, minute, second;
    datePartsFromEpoch(phoneEpochNow(), year, month, day, hour, minute, second);
    printAt(44, 218, ":" + twoDigit(second), COL_ROUTE, 3);
  } else {
    printAt(44, 218, watch.getTime(":%S"), COL_ROUTE, 3);
  }
  printAt(132, 226, fit(bridgeDateText() + "  " + bridgeDowText(), 22), COL_MUTED, 2);
}

void drawClockScreen() {
  gfx->fillScreen(COL_CLOCK_BG);
  drawPageTitle("SAFE HOUSE", phoneTimeValid() ? "Phone synced clock" : "Clock / weather / phone status");
  if (clockFaceId == 4) {
    drawBasicClockFace();
  } else if (clockFaceId == 0) {
    drawDigitalClockFace();
  } else {
    drawAnalogClockFace(clockFaceId);
  }
  drawClockStats();
  drawClockFooter();
  setFixedText(lastClockMinuteText, sizeof(lastClockMinuteText), bridgeTimeText(false));
}

void drawClockTick() {
  if (clockFaceId != 0) {
    drawClockScreen();
    return;
  }
  String minuteText = bridgeTimeText(false);
  if (minuteText != String(lastClockMinuteText)) {
    gfx->fillRect(34, 142, 270, 58, COL_BLACK);
    printAt(42, 148, minuteText, COL_TEXT, 5);
    setFixedText(lastClockMinuteText, sizeof(lastClockMinuteText), minuteText);
  }
  gfx->fillRect(38, 214, 88, 36, COL_BLACK);
  if (phoneTimeValid()) {
    int year, month, day, hour, minute, second;
    datePartsFromEpoch(phoneEpochNow(), year, month, day, hour, minute, second);
    printAt(44, 218, ":" + twoDigit(second), COL_ROUTE, 3);
  } else {
    printAt(44, 218, watch.getTime(":%S"), COL_ROUTE, 3);
  }
}

void drawCalendarScreen() {
  gfx->fillScreen(COL_CLOCK_BG);
  drawPageTitle("CALENDAR", profile.timezone);
  drawSoftPanel(20, 112, LCD_WIDTH - 40, 282, COL_BLACK, COL_ROUTE);

  int year = 2026, month = 1, day = 1, hour = 0, minute = 0, second = 0;
  if (phoneTimeValid()) {
    datePartsFromEpoch(phoneEpochNow(), year, month, day, hour, minute, second);
  }
  static const char *months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  printAt(42, 136, bridgeDowText(), COL_ROUTE, 3);
  printAt(42, 184, months[constrain(month - 1, 0, 11)], COL_TEXT, 5);
  printAt(184, 184, String(day), COL_TEXT, 5);
  printAt(44, 266, String(year) + "  " + bridgeTimeText(false), COL_MUTED, 2);
  gfx->drawFastHLine(42, 310, 324, COL_ROUTE);
  printAt(44, 326, fit(String("Safe House: ") + profile.home, 32), COL_SAFE, 1);
  printAt(44, 352, fit(String("Work: ") + profile.work, 32), COL_ROUTE, 1);
  printAt(44, 378, String(syncedMessageCount()) + " texts  " + String(syncedCallCount()) + " calls", COL_MUTED, 1);
  printAt(30, LCD_HEIGHT - 42, "Swipe: fitness / alarms", COL_MUTED, 1);
}

void drawAlarmsScreen() {
  gfx->fillScreen(COL_SETTINGS_BG);
  drawPageTitle("ALARMS", "Phone managed reminders");
  drawSoftPanel(20, 124, LCD_WIDTH - 40, 170, COL_BLACK, profile.alarmEnabled ? COL_SAFE : COL_ROUTE);
  printAt(42, 150, profile.alarmEnabled ? "ACTIVE" : "OFF", profile.alarmEnabled ? COL_SAFE : COL_ROUTE, 2);
  printAt(42, 188, fit(profile.alarmTime, 8), COL_TEXT, 5);
  printAt(44, 258, fit(profile.alarmLabel, 26), COL_MUTED, 2);
  drawSettingRow(326, "Timezone", profile.timezone, phoneTimeValid());
  drawSettingRow(382, "Date", bridgeDateText(), phoneTimeValid());
  printAt(30, LCD_HEIGHT - 42, "Change alarms in phone Settings.", COL_MUTED, 1);
}

int syncedContactCount() {
  int count = 0;
  for (int i = 0; i < SYNC_CONTACT_COUNT; i++) if (syncedContacts[i].valid) count++;
  return count;
}

int syncedCallCount() {
  int count = 0;
  for (int i = 0; i < SYNC_CALL_COUNT; i++) if (syncedCalls[i].valid) count++;
  return count;
}

int syncedMessageCount() {
  int count = 0;
  for (int i = 0; i < SYNC_MESSAGE_COUNT; i++) if (syncedMessages[i].valid) count++;
  return count;
}

int syncedPhotoCount() {
  int count = 0;
  for (int i = 0; i < SYNC_PHOTO_COUNT; i++) if (syncedPhotos[i].valid) count++;
  return count;
}

void drawNotificationsScreen() {
  gfx->fillScreen(COL_MSG_BG);
  drawTopBar();
  printAt(24, 76, "MESSAGES", COL_ROUTE, 2);

  int count = watch.getNotificationCount();
  int appMessages = syncedMessageCount();
  if (count == 0 && appMessages == 0) {
    drawSoftPanel(24, 146, LCD_WIDTH - 48, 132, COL_PANEL, COL_ROUTE);
    printAt(44, 174, "No messages yet", COL_TEXT, 2);
    printAt(44, 214, "Link Texts in phone app.", COL_MUTED, 1);
    drawMessagesFooter();
    return;
  }

  int y = 116;
  for (int i = 0; i < min(appMessages, 2); i++) {
    if (!syncedMessages[i].valid) continue;
    drawSoftPanel(18, y, LCD_WIDTH - 36, 76, COL_PANEL_2, COL_ROUTE);
    printAt(32, y + 10, fit(syncedMessages[i].from, 20), COL_ROUTE, 1);
    printAt(32, y + 32, fit(rewritePlaceTerms(syncedMessages[i].text), 42), COL_TEXT, 1);
    y += 86;
  }
  for (int i = 0; i < min(count, 4 - min(appMessages, 2)); i++) {
    Notification &n = watch.getNotificationAt(i);
    drawSoftPanel(18, y, LCD_WIDTH - 36, 76, (i == 0) ? COL_PANEL_2 : COL_PANEL, (i == 0) ? COL_ROUTE : 0x4A49);
    printAt(32, y + 10, fit(n.app.length() ? n.app : "Notification", 20), COL_ROUTE, 1);
    printAt(32, y + 30, fit(rewritePlaceTerms(n.title), 30), COL_TEXT, 1);
    printAt(32, y + 50, fit(rewritePlaceTerms(n.message), 43), COL_MUTED, 1);
    y += 86;
  }
  drawMessagesFooter();
}

void drawMessageDetailScreen() {
  gfx->fillScreen(COL_MSG_BG);
  drawTopBar();
  printAt(24, 76, "MESSAGE DETAIL", COL_ROUTE, 2);
  int count = watch.getNotificationCount();
  if (syncedMessages[0].valid) {
    drawSoftPanel(24, 126, LCD_WIDTH - 48, 242, COL_PANEL, COL_ROUTE);
    printAt(44, 150, fit(syncedMessages[0].from, 24), COL_ROUTE, 1);
    printAt(44, 190, fit(rewritePlaceTerms(syncedMessages[0].text), 38), COL_TEXT, 2);
    printAt(44, 248, fit(rewritePlaceTerms(String(syncedMessages[0].text).substring(38)), 42), COL_MUTED, 1);
    printAt(44, 310, "Reply from phone app.", COL_ROUTE, 1);
    drawMessagesFooter();
    return;
  }
  if (count <= 0) {
    drawSoftPanel(24, 146, LCD_WIDTH - 48, 132, COL_PANEL, COL_ROUTE);
    printAt(44, 174, "No active thread", COL_TEXT, 2);
    printAt(44, 214, "Swipe up/down: messages", COL_MUTED, 1);
    return;
  }

  Notification &n = watch.getNotificationAt(0);
  drawSoftPanel(24, 126, LCD_WIDTH - 48, 242, COL_PANEL, COL_ROUTE);
  printAt(44, 150, fit(n.app.length() ? n.app : "Notification", 24), COL_ROUTE, 1);
  printAt(44, 182, fit(rewritePlaceTerms(n.title), 28), COL_TEXT, 2);
  printAt(44, 232, fit(rewritePlaceTerms(n.message), 42), COL_MUTED, 1);
  printAt(44, 258, fit(rewritePlaceTerms(n.message.substring(42)), 42), COL_MUTED, 1);
  printAt(44, 310, "Reply/actions need app bridge.", COL_ROUTE, 1);
  drawMessagesFooter();
}

void drawPhoneScreen() {
  gfx->fillScreen(COL_PHONE_BG);
  drawTopBar();
  printAt(24, 76, "PHONE", COL_ROUTE, 2);
  PhoneInfo &phone = watch.getPhoneInfo();
  int contactCount = watch.getContactCount();
  int appContactCount = syncedContactCount();
  printAt(24, 104, bleConnected ? fit(phone.manufacturer + " " + phone.model, 34) : "Link phone app contacts", bleConnected ? COL_SAFE : COL_MUTED, 1);

  int rows = min(appContactCount + contactCount + GTA_CONTACT_COUNT, 6);
  for (int i = 0; i < rows; i++) {
    int y = 138 + i * 54;
    drawSoftPanel(20, y, LCD_WIDTH - 40, 42, i == 0 ? 0x3008 : COL_PANEL, i == 0 ? COL_ROUTE : 0x4A49);
    if (i < appContactCount) {
      SyncedContact contact = syncedContacts[i];
      printAt(42, y + 8, fit(contact.name, 23), i == 0 ? COL_ROUTE : COL_TEXT, 1);
      printAt(42, y + 26, fit(contact.number, 28), COL_MUTED, 1);
    } else if (i - appContactCount < contactCount) {
      Contact &contact = watch.getContact(i - appContactCount);
      printAt(42, y + 8, fit(contact.name.length() ? contact.name : "Contact", 22), i == 0 ? COL_ROUTE : COL_TEXT, 1);
      printAt(42, y + 26, fit(contact.number, 28), COL_MUTED, 1);
    } else {
      WatchContact contact = gtaContacts[i - appContactCount - contactCount];
      printAt(42, y + 8, fit(contact.name, 23), i == 0 ? COL_ROUTE : COL_TEXT, 1);
      printAt(42, y + 26, fit(contact.role, 30), COL_MUTED, 1);
    }
  }

  if (contactCount > 0 && watch.getSOSContactIndex() >= 0) {
    Contact &sos = watch.getSoSContact();
    printAt(30, LCD_HEIGHT - 42, "SOS: " + fit(sos.name, 22), COL_ROUTE, 1);
  } else {
    printAt(30, LCD_HEIGHT - 42, "Tap phone app to text/call contacts.", COL_MUTED, 1);
  }
}

void drawMessagesFooter() {
  int contactCount = watch.getContactCount();
  if (contactCount > 0) {
    printAt(24, LCD_HEIGHT - 36, String(contactCount) + " contacts synced", COL_MUTED, 1);
  }
}

void drawRecentCallsScreen() {
  gfx->fillScreen(COL_PHONE_BG);
  drawTopBar();
  printAt(24, 76, "RECENTS", COL_ROUTE, 2);
  printAt(24, 108, "Phone app calls and pinned contacts", COL_MUTED, 1);
  int contactCount = watch.getContactCount();
  int appCallCount = syncedCallCount();
  int rows = min(appCallCount + contactCount + GTA_CONTACT_COUNT, 5);
  for (int i = 0; i < rows; i++) {
    int y = 138 + i * 56;
    drawSoftPanel(20, y, LCD_WIDTH - 40, 44, COL_PANEL, 0x4A49);
    if (i < appCallCount) {
      SyncedCall call = syncedCalls[i];
      printAt(42, y + 8, fit(call.name, 24), COL_TEXT, 1);
      printAt(42, y + 26, fit(String(call.kind) + "  " + String(call.number), 30), COL_MUTED, 1);
    } else if (i - appCallCount < contactCount) {
      Contact &contact = watch.getContact(i - appCallCount);
      printAt(42, y + 8, fit(contact.name, 24), COL_TEXT, 1);
      printAt(42, y + 26, fit(contact.number, 30), COL_MUTED, 1);
    } else {
      WatchContact contact = gtaContacts[i - appCallCount - contactCount];
      printAt(42, y + 8, fit(contact.name, 24), COL_TEXT, 1);
      printAt(42, y + 26, fit(contact.number, 30), COL_MUTED, 1);
    }
  }
}

void drawGalleryScreen() {
  gfx->fillScreen(COL_PHONE_BG);
  drawTopBar();
  printAt(24, 76, "GALLERY", COL_ROUTE, 2);
  printAt(24, 108, "Phone album index on SD", COL_MUTED, 1);
  int count = syncedPhotoCount();
  if (count == 0) {
    drawSoftPanel(24, 152, LCD_WIDTH - 48, 156, COL_PANEL, COL_ROUTE);
    printAt(44, 178, "No album linked", COL_TEXT, 2);
    printAt(44, 220, "Use phone Gallery > Link.", COL_MUTED, 1);
    printAt(44, 248, "Thumbnails come next.", COL_MUTED, 1);
    return;
  }
  for (int i = 0; i < min(count, 5); i++) {
    if (!syncedPhotos[i].valid) continue;
    int y = 138 + i * 60;
    drawSoftPanel(20, y, LCD_WIDTH - 40, 48, COL_PANEL, 0x4A49);
    gfx->fillRect(38, y + 10, 28, 28, COL_ROUTE);
    gfx->fillRect(42, y + 14, 20, 20, COL_BLACK);
    printAt(78, y + 8, fit(syncedPhotos[i].label, 24), COL_TEXT, 1);
    printAt(78, y + 28, fit(syncedPhotos[i].album, 28), COL_MUTED, 1);
  }
  printAt(30, LCD_HEIGHT - 42, "Swipe up: Phone", COL_MUTED, 1);
}

void drawPlacesScreen() {
  gfx->fillScreen(COL_BLACK);
  drawTopBar();
  printAt(24, 72, "FAST TRAVEL", COL_ROUTE, 2);
  printAt(24, 100, "Swipe list. Tap row to select.", COL_MUTED, 1);

  int y = 124 - placesScroll;
  for (int i = 0; i < PLACE_COUNT; i++) {
    bool selected = !routeToParking && i == selectedPlace;
    if (y > 108 && y < LCD_HEIGHT - 48) {
      gfx->fillRect(18, y, LCD_WIDTH - 36, 42, selected ? 0x3008 : COL_PANEL);
      gfx->drawRect(18, y, LCD_WIDTH - 36, 42, selected ? COL_ROUTE : 0x4A49);
      gfx->fillCircle(40, y + 21, 7, places[i].color);
      printAt(60, y + 10, places[i].label, selected ? COL_ROUTE : COL_TEXT, 2);
      if (selected) {
        printAt(LCD_WIDTH - 74, y + 16, "ROUTED", COL_SAFE, 1);
      }
    }
    y += 47;
  }

  if (y > 108 && y < LCD_HEIGHT - 48) {
    gfx->fillRect(18, y, LCD_WIDTH - 36, 42, routeToParking ? 0x3008 : COL_PANEL);
    gfx->drawRect(18, y, LCD_WIDTH - 36, 42, routeToParking ? COL_ROUTE : 0x4A49);
    gfx->fillCircle(40, y + 21, 7, 0x66FF);
    printAt(60, y + 10, "Parked Car", routeToParking ? COL_ROUTE : COL_TEXT, 2);
    printAt(LCD_WIDTH - 92, y + 16, parkingSaved ? "SAVED" : "EMPTY", parkingSaved ? COL_SAFE : COL_RED, 1);
  }
  y += 47;

  if (y > 108 && y < LCD_HEIGHT - 48) {
    gfx->fillRect(18, y, LCD_WIDTH - 36, 42, COL_PANEL);
    gfx->drawRect(18, y, LCD_WIDTH - 36, 42, 0x4A49);
    printAt(34, y + 10, "Mechanic", COL_TEXT, 2);
    printAt(LCD_WIDTH - 116, y + 16, "FIND RIDE", COL_ROUTE, 1);
  }
}

String weatherLabel(int icon) {
  switch (icon) {
    case 1:
      return "Sunny";
    case 2:
      return "Snow";
    case 3:
      return "Rain";
    case 4:
      return "Cloudy";
    case 5:
      return "Extreme";
    case 6:
      return "Wind";
    case 7:
      return "Fog";
    default:
      return "Partly cloudy";
  }
}

void drawWeatherScreen() {
  gfx->fillScreen(COL_CLOCK_BG);
  drawPageTitle("WEATHER", phoneWeather.valid ? phoneWeather.city : "Phone forecast");
  drawSoftPanel(20, 124, LCD_WIDTH - 40, 192, COL_BLACK, COL_ROUTE);
  int weatherCount = watch.getWeatherCount();
  if (phoneWeather.valid) {
    unsigned long ageMin = phoneWeather.updatedMs == 0 ? 0 : (millis() - phoneWeather.updatedMs) / 60000UL;
    printAt(42, 148, fit(phoneWeather.city, 21), COL_TEXT, 2);
    printAt(42, 188, String(phoneWeather.temp), COL_ROUTE, 5);
    printAt(152, 204, fit(phoneWeather.label, 14), COL_TEXT, 2);
    printAt(44, 256, "High " + String(phoneWeather.high) + "  Low " + String(phoneWeather.low), COL_MUTED, 2);
    printAt(44, 286, "Phone weather  " + String(ageMin) + "m ago", ageMin < 45 ? COL_SAFE : COL_ROUTE, 1);
  } else if (weatherCount > 0) {
    Weather &weather = watch.getWeatherAt(0);
    printAt(42, 148, fit(watch.getWeatherCity(), 21), COL_TEXT, 2);
    printAt(42, 188, String(weather.temp), COL_ROUTE, 5);
    printAt(152, 204, weatherLabel(weather.icon), COL_TEXT, 2);
    printAt(44, 256, "High " + String(weather.high) + "  Low " + String(weather.low), COL_MUTED, 2);
    printAt(44, 286, fit(watch.getWeatherTime(), 34), COL_MUTED, 1);
  } else {
    printAt(44, 154, "Weather waiting", COL_TEXT, 2);
    printAt(44, 196, "Use phone Settings.", COL_MUTED, 1);
    printAt(44, 224, "Sync Weather to watch.", COL_MUTED, 1);
  }
  drawStatTile(22, 342, 174, "GPS", liveGps.valid ? fit(gpsLine(liveGps), 16) : "WAITING", liveGps.valid ? COL_SAFE : COL_ROUTE);
  drawStatTile(214, 342, 174, "LINK", bleConnected ? "PHONE" : "OFFLINE", bleConnected ? COL_SAFE : COL_RED);
  printAt(30, LCD_HEIGHT - 42, "Swipe: clock / fitness", COL_MUTED, 1);
}

void drawFitnessScreen() {
  gfx->fillScreen(COL_CLOCK_BG);
  drawPageTitle("PEDOMETER", fitnessSourceText().c_str());
  uint32_t goal = effectiveStepGoal();
  uint32_t steps = effectiveSteps();
  uint32_t pct = steps >= goal ? 100 : (steps * 100UL) / goal;
  drawSoftPanel(20, 112, LCD_WIDTH - 40, 216, COL_BLACK, COL_SAFE);
  printAt(42, 138, String(steps), COL_TEXT, 5);
  printAt(46, 218, "STEPS", COL_ROUTE, 2);
  gfx->fillRect(42, 270, 324, 18, 0x2965);
  gfx->fillRect(44, 272, pct * 320 / 100, 14, COL_SAFE);
  gfx->drawRect(42, 270, 324, 18, COL_BLACK);
  printAt(46, 296, String(pct) + "% of " + String(goal), COL_MUTED, 1);
  drawStatTile(22, 352, 112, "MILES", steps > 0 ? String(effectiveDistanceMiles(), 2) : "--", COL_ROUTE);
  drawStatTile(150, 352, 112, "CAL", steps > 0 ? String(effectiveCalories()) : "--", COL_ROUTE);
  drawStatTile(278, 352, 110, "ACTIVE", steps > 0 ? String(effectiveActiveMinutes()) + "m" : "--", COL_SAFE);
  unsigned long ageMin = phoneFitness.updatedMs == 0 ? 0 : (millis() - phoneFitness.updatedMs) / 60000UL;
  String status = qmiReady ? "Watch IMU counter active" : (phoneFitness.valid ? "Phone updated " + String(ageMin) + "m ago" : "No steps yet");
  printAt(30, LCD_HEIGHT - 66, status, qmiReady || phoneFitness.valid ? COL_SAFE : COL_ROUTE, 1);
  printAt(30, LCD_HEIGHT - 42, "Swipe: weather / calendar", COL_MUTED, 1);
}

void drawSettingsScreen() {
  gfx->fillScreen(COL_SETTINGS_BG);
  drawPageTitle("SETTINGS", "Quick controls and live status");

  PhoneInfo &phone = watch.getPhoneInfo();
  drawToggleRow(128, "Theme", String("THEME ") + String(activeTheme), activeTheme != 2);
  drawSettingRow(196, "Clock", bridgeTimeText(false), phoneTimeValid());
  drawSettingRow(252, "Phone", bleConnected ? fit(phone.model, 20) : "Not linked");
  drawSettingRow(308, "TZ", profile.timezone, phoneTimeValid());
  drawSettingRow(364, "Alarm", profile.alarmEnabled ? String(profile.alarmTime) : "Off", profile.alarmEnabled);

  printAt(30, LCD_HEIGHT - 42, "Tap Theme. Swipe up/down: About.", bleConnected ? COL_SAFE : COL_ROUTE, 1);
}

void drawAboutScreen() {
  gfx->fillScreen(COL_SETTINGS_BG);
  drawTopBar();
  printAt(24, 76, "ABOUT / DEBUG", COL_ROUTE, 2);
  PhoneInfo &phone = watch.getPhoneInfo();
  drawSoftPanel(16, 112, LCD_WIDTH - 32, 366, COL_BLACK, COL_ROUTE);
  printAt(34, 132, "GTA-Nav", COL_TEXT, 3);
  printAt(34, 174, bleConnected ? "BLE LINKED" : "BLE OFFLINE", bleConnected ? COL_SAFE : COL_RED, 2);
  printAt(34, 208, "RAW " + String(rawCommandCount), COL_TEXT, 2);
  printAt(194, 208, "GPS " + String(gpsCommandCount), gpsCommandCount > 0 ? COL_SAFE : COL_ROUTE, 2);
  printAt(34, 242, "HEAD " + String(headCommandCount), headCommandCount > 0 ? COL_SAFE : COL_ROUTE, 2);
  printAt(194, 242, "POI " + String(poiCommandCount), poiCommandCount > 0 ? COL_SAFE : COL_ROUTE, 2);
  printAt(34, 276, sdMounted ? "SD OK" : "SD FAIL", sdMounted ? COL_SAFE : COL_RED, 2);
  printAt(194, 276, watch.isSubscribed() ? "SUB YES" : "SUB NO", watch.isSubscribed() ? COL_SAFE : COL_ROUTE, 2);
  printAt(34, 310, "ADDR " + fit(watch.getAddress(), 26), COL_MUTED, 1);
  printAt(34, 334, "PHONE " + fit(phone.manufacturer + " " + phone.model, 25), COL_MUTED, 1);
  printAt(34, 358, "BAT " + String(watch.getPhoneBattery()) + "%  MSG " + String(watch.getNotificationCount()), COL_MUTED, 1);
  printAt(34, 382, liveGps.valid ? fit(gpsLine(liveGps), 40) : "GPS WAITING", liveGps.valid ? COL_SAFE : COL_ROUTE, 1);
  unsigned long age = lastRawCommandMs == 0 ? 0 : (millis() - lastRawCommandMs) / 1000;
  printAt(34, 408, "LAST " + fit(String(lastRawCommand), 33), COL_TEXT, 1);
  printAt(34, 432, "AGE " + String(age) + "s  APP " + fit(watch.getAppVersion(), 18), COL_MUTED, 1);
  printAt(34, 456, "Swipe down: Settings", COL_ROUTE, 1);
}

void drawMechanicScreen() {
  gfx->fillScreen(COL_BLACK);
  drawTopBar();
  printAt(24, 76, "MECHANIC", COL_ROUTE, 2);
  gfx->fillRect(22, 126, LCD_WIDTH - 44, 166, COL_PANEL);
  gfx->drawRect(22, 126, LCD_WIDTH - 44, 166, COL_ROUTE);
  printAt(42, 150, "Dispatch here.", COL_TEXT, 2);
  printAt(42, 184, "Want the ride marked?", COL_TEXT, 2);
  printAt(42, 224, parkingSaved ? "Parked Car is on the map." : "No parked car saved yet.", parkingSaved ? COL_SAFE : COL_RED, 1);
  printAt(42, 250, parkingSaved ? gpsLine(parkedGps) : "Go to Places > Parked Car.", COL_MUTED, 1);

  gfx->fillRect(38, 334, LCD_WIDTH - 76, 54, parkingSaved ? 0x3008 : 0x2124);
  gfx->drawRect(38, 334, LCD_WIDTH - 76, 54, parkingSaved ? COL_ROUTE : COL_MUTED);
  printAt(68, 352, parkingSaved ? "SHOW PARKED CAR" : "SAVE CURRENT PARK", parkingSaved ? COL_ROUTE : COL_TEXT, 2);
  printAt(34, LCD_HEIGHT - 42, "Tap button. Top bar cycles screens.", COL_MUTED, 1);
}

void drawLauncherStatusBar() {
  gfx->fillRect(0, 0, LCD_WIDTH, 58, COL_BLACK);
  printAt(16, 18, bridgeTimeText(false), COL_TEXT, 2);
  printAt(164, 20, "GTA-NAV", COL_SAFE, 2);
  gfx->fillCircle(316, 28, 3, bleConnected ? COL_SAFE : COL_ROUTE);
  printAt(328, 22, bleConnected ? "ON" : "PAIR", bleConnected ? COL_SAFE : COL_ROUTE, 1);
  gfx->drawRect(368, 21, 28, 14, COL_TEXT);
  gfx->fillRect(397, 25, 3, 6, COL_TEXT);
  int fillW = constrain(watch.getPhoneBattery(), 0, 100) * 28 / 100;
  gfx->fillRect(371, 24, fillW, 8, watch.isPhoneCharging() ? COL_SAFE : COL_TEXT);
}

void drawIconMask(int x, int y, const uint8_t *mask, uint16_t color) {
  for (int py = 0; py < WATCH_MASK_ICON_H; py++) {
    for (int px = 0; px < WATCH_MASK_ICON_W; px++) {
      int bit = py * WATCH_MASK_ICON_W + px;
      uint8_t b = pgm_read_byte(mask + (bit / 8));
      if ((b >> (7 - (bit % 8))) & 0x01) {
        gfx->drawPixel(x + px, y + py, color);
      }
    }
  }
}

const uint16_t *launcherAssetFor(Screen target) {
  if (target == SCREEN_MAP) return ASSET_TRACKIFY;
  if (target == SCREEN_PHONE) return ASSET_CONTACTS;
  if (target == SCREEN_NOTIFICATIONS) return ASSET_MESSAGES;
  if (target == SCREEN_SETTINGS) return ASSET_SETTINGS;
  if (target == SCREEN_BUDDY) return ASSET_BUDDY;
  if (target == SCREEN_WEATHER) return ASSET_WEATHER;
  if (target == SCREEN_FITNESS) return ASSET_CALENDAR;
  if (target == SCREEN_CALENDAR) return ASSET_CALENDAR;
  if (target == SCREEN_CLOCK) return ASSET_CALENDAR;
  if (target == SCREEN_MECHANIC) return ASSET_MECHANIC;
  return nullptr;
}

void drawLauncherIconArt(int cx, int cy, Screen target, uint16_t accent) {
  const uint16_t *asset = launcherAssetFor(target);
  if (asset) {
    gfx->draw16bitRGBBitmap(cx - WATCH_ICON_W / 2, cy - WATCH_ICON_H / 2, asset, WATCH_ICON_W, WATCH_ICON_H);
    return;
  }

  const uint8_t *mask = nullptr;
  if (target == SCREEN_MAP) mask = MASK_TRACKIFY;
  else if (target == SCREEN_PHONE) mask = MASK_CONTACTS;
  else if (target == SCREEN_NOTIFICATIONS) mask = MASK_MESSAGES;
  else if (target == SCREEN_SETTINGS) mask = MASK_SETTINGS;
  else if (target == SCREEN_BUDDY) mask = MASK_BUDDY;
  else if (target == SCREEN_WEATHER) mask = MASK_WEATHER;
  else if (target == SCREEN_CALENDAR) mask = MASK_CALENDAR;
  else if (target == SCREEN_MECHANIC) mask = MASK_MECHANIC;

  if (mask) {
    drawIconMask(cx - WATCH_MASK_ICON_W / 2, cy - WATCH_MASK_ICON_H / 2, mask, accent);
    return;
  }

  if (target == SCREEN_MAP) {
    gfx->fillCircle(cx, cy, 38, COL_BLACK);
    gfx->drawCircle(cx, cy, 39, COL_SAFE);
    gfx->drawCircle(cx, cy, 34, 0x4D76);
    thickLine(cx - 24, cy - 4, cx + 26, cy - 16, COL_MAP_ROAD, 8);
    thickLine(cx - 26, cy + 18, cx + 20, cy + 22, COL_MAP_ROAD_MINOR, 6);
    gfx->fillTriangle(cx, cy - 14, cx - 10, cy + 14, cx, cy + 8, COL_PLAYER);
    gfx->fillTriangle(cx, cy - 14, cx, cy + 8, cx + 10, cy + 14, COL_PLAYER);
    return;
  }
  if (target == SCREEN_PHONE) {
    gfx->drawCircle(cx - 18, cy - 18, 13, COL_TEXT);
    gfx->drawCircle(cx + 18, cy - 18, 13, COL_TEXT);
    gfx->drawCircle(cx - 18, cy + 18, 13, COL_TEXT);
    gfx->drawCircle(cx + 18, cy + 18, 13, COL_TEXT);
    gfx->fillRect(cx - 22, cy - 2, 44, 4, COL_TEXT);
    gfx->fillRect(cx - 2, cy - 22, 4, 44, COL_TEXT);
    return;
  }
  if (target == SCREEN_NOTIFICATIONS) {
    fillSoftRoundRect(cx - 34, cy - 26, 68, 52, 12, COL_TEXT);
    gfx->fillTriangle(cx - 20, cy + 18, cx - 6, cy + 18, cx - 20, cy + 34, COL_TEXT);
    gfx->fillRect(cx - 22, cy - 8, 44, 5, accent);
    gfx->fillRect(cx - 22, cy + 8, 34, 5, accent);
    return;
  }
  if (target == SCREEN_SETTINGS) {
    for (int i = 0; i < 8; i++) {
      float a = i * PI / 4.0f;
      int x1 = cx + (int)(cos(a) * 22);
      int y1 = cy + (int)(sin(a) * 22);
      int x2 = cx + (int)(cos(a) * 35);
      int y2 = cy + (int)(sin(a) * 35);
      thickLine(x1, y1, x2, y2, COL_TEXT, 8);
    }
    gfx->fillCircle(cx, cy, 28, COL_TEXT);
    gfx->fillCircle(cx, cy, 12, accent);
    return;
  }
  if (target == SCREEN_BUDDY) {
    gfx->fillCircle(cx, cy - 4, 28, COL_TEXT);
    gfx->fillCircle(cx - 10, cy - 8, 5, accent);
    gfx->fillCircle(cx + 10, cy - 8, 5, accent);
    gfx->drawFastHLine(cx - 12, cy + 10, 24, accent);
    gfx->fillRect(cx - 20, cy + 27, 40, 8, COL_TEXT);
    gfx->fillCircle(cx - 30, cy + 2, 6, COL_TEXT);
    gfx->fillCircle(cx + 30, cy + 2, 6, COL_TEXT);
    return;
  }
  if (target == SCREEN_WEATHER) {
    gfx->fillCircle(cx - 10, cy + 8, 24, COL_TEXT);
    gfx->fillCircle(cx + 14, cy + 5, 19, COL_TEXT);
    gfx->fillCircle(cx + 30, cy + 12, 14, COL_TEXT);
    gfx->fillRect(cx - 30, cy + 10, 64, 20, COL_TEXT);
    gfx->fillCircle(cx + 22, cy - 20, 13, accent);
    return;
  }
  if (target == SCREEN_FITNESS) {
    gfx->drawCircle(cx, cy, 35, COL_TEXT);
    gfx->fillCircle(cx - 12, cy - 10, 8, accent);
    thickLine(cx - 12, cy - 2, cx + 8, cy + 8, COL_TEXT, 8);
    thickLine(cx + 8, cy + 8, cx + 26, cy - 16, accent, 6);
    thickLine(cx + 6, cy + 10, cx - 18, cy + 28, COL_TEXT, 6);
    return;
  }
  if (target == SCREEN_CALENDAR) {
    fillSoftRoundRect(cx - 34, cy - 34, 68, 68, 10, COL_TEXT);
    gfx->fillRect(cx - 34, cy - 34, 68, 16, accent);
    printAt(cx - 20, cy - 4, "24", COL_BLACK, 3);
    return;
  }
  if (target == SCREEN_MECHANIC) {
    thickLine(cx - 26, cy + 20, cx + 26, cy - 20, COL_TEXT, 12);
    gfx->fillCircle(cx - 30, cy + 24, 12, COL_TEXT);
    gfx->fillCircle(cx + 30, cy - 24, 12, COL_TEXT);
    gfx->fillCircle(cx - 30, cy + 24, 6, accent);
    gfx->fillCircle(cx + 30, cy - 24, 6, accent);
    return;
  }
  gfx->fillCircle(cx, cy, 30, COL_TEXT);
}

void drawLauncherIcon(int x, int y, const char *label, Screen target, uint16_t fill, uint16_t accent) {
  (void)fill;
  gfx->fillRect(x + 10, y, 112, 82, 0x0841);
  gfx->drawRect(x + 10, y, 112, 82, accent);
  gfx->drawRect(x + 13, y + 3, 106, 76, accent);
  drawLauncherIconArt(x + 66, y + 41, target, accent);
  int labelX = x + 66 - (strlen(label) * 12) / 2;
  printAt(max(0, labelX), y + 94, label, COL_TEXT, 2);
}

void drawLauncherScreen() {
  gfx->fillScreen(COL_BLACK);
  drawLauncherStatusBar();
  if (launcherPage == 0) {
    drawLauncherIcon(50, 82, "Trackify", SCREEN_MAP, 0x0228, COL_SAFE);
    drawLauncherIcon(228, 82, "Clock", SCREEN_CLOCK, 0x03BF, 0x66FF);
    drawLauncherIcon(50, 268, "Steps", SCREEN_FITNESS, 0xE5A0, COL_ROUTE);
    drawLauncherIcon(228, 268, "Contacts", SCREEN_PHONE, 0x03BF, 0x66FF);
  } else {
    drawLauncherIcon(50, 82, "Messages", SCREEN_NOTIFICATIONS, 0xE5A0, COL_ROUTE);
    drawLauncherIcon(228, 82, "Buddy", SCREEN_BUDDY, 0x18E3, 0x66FF);
    drawLauncherIcon(50, 268, "Weather", SCREEN_WEATHER, 0x03BF, COL_SAFE);
    drawLauncherIcon(228, 268, "Settings", SCREEN_SETTINGS, 0x04DF, 0x66FF);
  }

  if (launcherPage == 0) {
    gfx->fillRect(154, 460, 62, 8, COL_TEXT);
    gfx->fillCircle(244, 465, 8, COL_MUTED);
  } else {
    gfx->fillCircle(166, 465, 8, COL_MUTED);
    gfx->fillRect(194, 460, 62, 8, COL_TEXT);
  }
  gfx->drawFastHLine(96, 486, LCD_WIDTH - 192, COL_TEXT);
}

void drawBuddyScreen() {
  gfx->fillScreen(COL_BLACK);
  printAt(24, 24, ": Buddy", COL_TEXT, 2);
  printAt(300, 24, String(watch.getPhoneBattery()) + "%", COL_TEXT, 2);
  printAt(268, 50, bleConnected ? "=PHONE ON" : "=PHONE OFF", bleConnected ? COL_SAFE : COL_TEXT, 1);

  if (buddyView == 1) {
    printAt(116, 112, "(o_o)", COL_ROUTE, 5);
    printAt(78, 218, "Speak naturally.", COL_TEXT, 2);
    printAt(78, 250, "Phone AI bridge required", COL_MUTED, 1);
    for (int i = 0; i < 18; i++) {
      int h = 10 + ((i * 17 + millis() / 60) % 44);
      gfx->fillRect(58 + i * 16, 350 - h, 9, h, COL_ROUTE);
    }
    printAt(130, 382, "voice level live", COL_MUTED, 1);
  } else if (buddyView == 2) {
    printAt(40, 98, "you", COL_MUTED, 1);
    printAt(40, 126, "phone said", COL_TEXT, 2);
    printAt(40, 190, "Buddy", COL_ROUTE, 1);
    String line = String(buddyLine);
    printAt(40, 218, fit(line, 30), COL_TEXT, 1);
    printAt(40, 244, fit(line.length() > 30 ? line.substring(30) : "", 30), COL_TEXT, 1);
    printAt(40, 270, "Use phone mic for voice.", COL_MUTED, 1);
    printAt(40, 296, "Map/GPS bridge stays live.", COL_MUTED, 1);
  } else if (buddyView == 3) {
    printAt(40, 94, ": Type", COL_TEXT, 2);
    printAt(40, 136, "Use the phone app for", COL_TEXT, 1);
    printAt(40, 162, "text entry. Watch input", COL_TEXT, 1);
    printAt(40, 188, "will mirror here next.", COL_TEXT, 1);
    gfx->drawRect(38, 246, 334, 86, COL_ROUTE);
    printAt(56, 276, "Message waiting...", COL_MUTED, 2);
  } else {
    printAt(116, 124, "(^_^)", COL_ROUTE, 5);
    printAt(72, 224, "Hey, ready when you are.", COL_TEXT, 1);
  }

  gfx->fillRect(72, 304, 266, 54, 0xFBE0);
  gfx->drawRect(72, 304, 266, 54, 0xFD20);
  printAt(150, 321, "TALK TO ME", COL_BLACK, 2);

  gfx->fillRect(72, 374, 74, 32, buddyView == 2 ? 0xFBE0 : 0x39E7);
  gfx->fillRect(168, 374, 74, 32, buddyView == 3 ? 0xFBE0 : 0x39E7);
  gfx->fillRect(264, 374, 74, 32, 0x39E7);
  printAt(88, 382, "CHAT", buddyView == 2 ? COL_BLACK : COL_TEXT, 1);
  printAt(184, 382, "TYPE", buddyView == 3 ? COL_BLACK : COL_TEXT, 1);
  printAt(286, 382, "SET", COL_TEXT, 1);

  printAt(126, 428, bleConnected ? "phone bridge ready" : "pair phone for AI", bleConnected ? COL_SAFE : COL_MUTED, 1);
}

void drawScreen() {
  switch (activeScreen) {
    case SCREEN_LAUNCHER:
      drawLauncherScreen();
      break;
    case SCREEN_MAP:
      drawMapScreen();
      break;
    case SCREEN_NOTIFICATIONS:
      drawNotificationsScreen();
      break;
    case SCREEN_MESSAGE_DETAIL:
      drawMessageDetailScreen();
      break;
    case SCREEN_PHONE:
      drawPhoneScreen();
      break;
    case SCREEN_RECENTS:
      drawRecentCallsScreen();
      break;
    case SCREEN_GALLERY:
      drawGalleryScreen();
      break;
    case SCREEN_CLOCK:
      drawClockScreen();
      break;
    case SCREEN_SETTINGS:
      drawSettingsScreen();
      break;
    case SCREEN_BUDDY:
      drawBuddyScreen();
      break;
    case SCREEN_PLACES:
      drawPlacesScreen();
      break;
    case SCREEN_MECHANIC:
      drawMechanicScreen();
      break;
    case SCREEN_WEATHER:
      drawWeatherScreen();
      break;
    case SCREEN_FITNESS:
      drawFitnessScreen();
      break;
    case SCREEN_CALENDAR:
      drawCalendarScreen();
      break;
    case SCREEN_ALARMS:
      drawAlarmsScreen();
      break;
    case SCREEN_ABOUT:
      drawAboutScreen();
      break;
    default:
      activeScreen = SCREEN_MAP;
      drawMapScreen();
      break;
  }
  needsRedraw = false;
  lastClockDrawMs = millis();
}

void ensureMapFiles() {
  if (!sdMounted) {
    return;
  }
  if (!SD_MMC.exists("/gta_map")) {
    SD_MMC.mkdir("/gta_map");
  }
  File f = SD_MMC.open("/gta_map/places.txt", FILE_WRITE);
  if (f) {
    f.println("label,x,y");
    for (int i = 0; i < PLACE_COUNT; i++) {
      f.print(places[i].label);
      f.print(",");
      f.print(places[i].x);
      f.print(",");
      f.println(places[i].y);
    }
    f.println("Parked Car,live-gps,live-gps");
    f.println("Mechanic,route-to-parked-car,route-to-parked-car");
    f.close();
  }
  if (!SD_MMC.exists("/gta_map/README.TXT")) {
    File r = SD_MMC.open("/gta_map/README.TXT", FILE_WRITE);
    if (r) {
      r.println("GTA-Nav map data folder.");
      r.println("Current build uses built-in stylized road data plus Chronos navigation overlay.");
      r.println("Bridge commands: GPS,lat,lon,speed,heading | PARK | PARK,lat,lon,heading | THEME,theme1..3 | CLOCK,digital|analog|classic|onehand|basic|luxury | WEATHER,temp,hi,lo,label,city | FITNESS,steps,miles,cal,min,goal");
      r.println("Next build step: replace built-in roads with generated offline OSM road tiles.");
      r.close();
    }
  }
  if (!SD_MMC.exists("/gta_sync")) {
    SD_MMC.mkdir("/gta_sync");
  }
}

void saveSyncLine(const char *path, const String &line) {
  if (!sdMounted) return;
  if (!SD_MMC.exists("/gta_sync")) {
    SD_MMC.mkdir("/gta_sync");
  }
  File file = SD_MMC.open(path, FILE_APPEND);
  if (!file) return;
  file.println(line);
  file.close();
}

void connectionCallback(bool state) {
  bleConnected = state;
  USBSerial.println(state ? "BLE connected" : "BLE disconnected");
  if (activeScreen == SCREEN_MAP) {
    mapOverlayDirty = true;
  } else {
    needsRedraw = true;
  }
}

void notificationCallback(Notification notification) {
  (void)notification;
  if (activeScreen == SCREEN_MAP) {
    mapOverlayDirty = true;
  } else {
    needsRedraw = true;
  }
}

void configCallback(Config config, uint32_t a, uint32_t b) {
  (void)a;
  (void)b;
  if (config == CF_TIME) {
    if (activeScreen == SCREEN_CLOCK) {
      needsRedraw = true;
    }
    return;
  }

  if (config == CF_NAV_DATA || config == CF_NAV_ICON || config == CF_CONTACT || config == CF_SYNCED) {
    if (activeScreen == SCREEN_MAP) {
      mapOverlayDirty = true;
      return;
    }
    needsRedraw = true;
    return;
  }

  if (config == CF_PBAT || config == CF_WEATHER || config == CF_APP || config == CF_HR24) {
    if (activeScreen == SCREEN_MAP) {
      mapOverlayDirty = true;
    } else {
      needsRedraw = true;
    }
  }
}

float csvPartToFloat(const String &text, int index, float fallback) {
  int start = 0;
  int current = 0;
  while (current < index) {
    start = text.indexOf(',', start);
    if (start < 0) {
      return fallback;
    }
    start++;
    current++;
  }
  int end = text.indexOf(',', start);
  String part = end < 0 ? text.substring(start) : text.substring(start, end);
  part.trim();
  return part.length() ? part.toFloat() : fallback;
}

String csvPart(const String &text, int index) {
  int start = 0;
  int current = 0;
  while (current < index) {
    start = text.indexOf(',', start);
    if (start < 0) {
      return "";
    }
    start++;
    current++;
  }
  int end = text.indexOf(',', start);
  String part = end < 0 ? text.substring(start) : text.substring(start, end);
  part.trim();
  return part;
}

void applyVehicleCommand(const String &msg) {
  setFixedText(trip.vehicle, sizeof(trip.vehicle), csvPart(msg, 1));
  trip.tankGallons = csvPartToFloat(msg, 2, trip.tankGallons);
  trip.mpg = csvPartToFloat(msg, 3, trip.mpg);
  setFixedText(trip.fuelType, sizeof(trip.fuelType), csvPart(msg, 4));
  trip.gasPrice = csvPartToFloat(msg, 5, trip.gasPrice);
  prefs.putString("vehicle", trip.vehicle);
  prefs.putFloat("tankGal", trip.tankGallons);
  prefs.putFloat("mpg", trip.mpg);
  prefs.putString("fuelType", trip.fuelType);
  prefs.putFloat("gasPrice", trip.gasPrice);
}

void applyTripCommand(const String &msg) {
  setFixedText(trip.destination, sizeof(trip.destination), csvPart(msg, 1));
  String mode = csvPart(msg, 2);
  mode.toLowerCase();
  if (mode == "walk") {
    trip.driving = false;
  } else if (mode == "drive") {
    trip.driving = true;
  }
  trip.routeMiles = csvPartToFloat(msg, 3, trip.routeMiles);
  trip.tollCost = csvPartToFloat(msg, 4, trip.tollCost);
  trip.gasPrice = csvPartToFloat(msg, 5, trip.gasPrice);
  needsRedraw = true;
}

void applyTimeCommand(const String &msg) {
  unsigned long epoch = (unsigned long)csvPartToFloat(msg, 1, 0);
  long offset = (long)csvPartToFloat(msg, 2, profile.timezoneOffsetMinutes);
  setFixedText(profile.timezone, sizeof(profile.timezone), csvPart(msg, 3));
  setFixedText(profile.dateText, sizeof(profile.dateText), csvPart(msg, 4));
  if (epoch > 1600000000UL) {
    profile.epochAtSync = epoch + (offset * 60L);
    profile.syncedAtMs = millis();
    profile.timezoneOffsetMinutes = offset;
    prefs.putULong("epoch", profile.epochAtSync);
    prefs.putLong("tzOffset", profile.timezoneOffsetMinutes);
  }
  prefs.putString("tz", profile.timezone);
  prefs.putString("date", profile.dateText);
  needsRedraw = true;
}

void applyProfileCommand(const String &msg) {
  setFixedText(profile.home, sizeof(profile.home), csvPart(msg, 1));
  setFixedText(profile.work, sizeof(profile.work), csvPart(msg, 2));
  prefs.putString("home", profile.home);
  prefs.putString("work", profile.work);
  needsRedraw = true;
}

void applyAlarmCommand(const String &msg) {
  setFixedText(profile.alarmTime, sizeof(profile.alarmTime), csvPart(msg, 1));
  setFixedText(profile.alarmLabel, sizeof(profile.alarmLabel), csvPart(msg, 2));
  String enabled = csvPart(msg, 3);
  enabled.toLowerCase();
  profile.alarmEnabled = enabled == "1" || enabled == "true" || enabled == "on";
  prefs.putString("alarmTime", profile.alarmTime);
  prefs.putString("alarmLbl", profile.alarmLabel);
  prefs.putBool("alarmOn", profile.alarmEnabled);
  needsRedraw = true;
}

void applyWeatherCommand(const String &msg) {
  phoneWeather.temp = (int)csvPartToFloat(msg, 1, phoneWeather.temp);
  phoneWeather.high = (int)csvPartToFloat(msg, 2, phoneWeather.high);
  phoneWeather.low = (int)csvPartToFloat(msg, 3, phoneWeather.low);
  setFixedText(phoneWeather.label, sizeof(phoneWeather.label), csvPart(msg, 4));
  setFixedText(phoneWeather.city, sizeof(phoneWeather.city), csvPart(msg, 5));
  phoneWeather.valid = true;
  phoneWeather.updatedMs = millis();
  prefs.putInt("wxTemp", phoneWeather.temp);
  prefs.putInt("wxHigh", phoneWeather.high);
  prefs.putInt("wxLow", phoneWeather.low);
  prefs.putString("wxLabel", phoneWeather.label);
  prefs.putString("wxCity", phoneWeather.city);
  prefs.putBool("wxValid", true);
  needsRedraw = true;
}

void applyFitnessCommand(const String &msg) {
  phoneFitness.steps = (uint32_t)max(0.0f, csvPartToFloat(msg, 1, phoneFitness.steps));
  phoneFitness.distanceMiles = max(0.0f, csvPartToFloat(msg, 2, phoneFitness.distanceMiles));
  phoneFitness.calories = (uint16_t)constrain((int)csvPartToFloat(msg, 3, phoneFitness.calories), 0, 65535);
  phoneFitness.activeMinutes = (uint16_t)constrain((int)csvPartToFloat(msg, 4, phoneFitness.activeMinutes), 0, 65535);
  phoneFitness.goal = (uint32_t)max(1.0f, csvPartToFloat(msg, 5, phoneFitness.goal > 0 ? phoneFitness.goal : 8000));
  phoneFitness.valid = true;
  phoneFitness.updatedMs = millis();
  prefs.putUInt("fitSteps", phoneFitness.steps);
  prefs.putFloat("fitMiles", phoneFitness.distanceMiles);
  prefs.putUShort("fitCal", phoneFitness.calories);
  prefs.putUShort("fitMin", phoneFitness.activeMinutes);
  prefs.putUInt("fitGoal", phoneFitness.goal);
  prefs.putBool("fitValid", true);
  needsRedraw = true;
}

void applyContactCommand(const String &msg) {
  int index = constrain((int)csvPartToFloat(msg, 1, 0), 0, SYNC_CONTACT_COUNT - 1);
  setFixedText(syncedContacts[index].name, sizeof(syncedContacts[index].name), csvPart(msg, 2));
  setFixedText(syncedContacts[index].number, sizeof(syncedContacts[index].number), csvPart(msg, 3));
  setFixedText(syncedContacts[index].role, sizeof(syncedContacts[index].role), csvPart(msg, 4));
  syncedContacts[index].valid = strlen(syncedContacts[index].name) > 0;
  saveSyncLine("/gta_sync/contacts.csv", String(index) + "," + syncedContacts[index].name + "," + syncedContacts[index].number + "," + syncedContacts[index].role);
  needsRedraw = true;
}

void applyCallCommand(const String &msg) {
  int index = constrain((int)csvPartToFloat(msg, 1, 0), 0, SYNC_CALL_COUNT - 1);
  setFixedText(syncedCalls[index].name, sizeof(syncedCalls[index].name), csvPart(msg, 2));
  setFixedText(syncedCalls[index].number, sizeof(syncedCalls[index].number), csvPart(msg, 3));
  setFixedText(syncedCalls[index].kind, sizeof(syncedCalls[index].kind), csvPart(msg, 4));
  syncedCalls[index].valid = strlen(syncedCalls[index].name) > 0;
  saveSyncLine("/gta_sync/calls.csv", String(index) + "," + syncedCalls[index].name + "," + syncedCalls[index].number + "," + syncedCalls[index].kind);
  needsRedraw = true;
}

void applyMessageCommand(const String &msg) {
  int index = constrain((int)csvPartToFloat(msg, 1, 0), 0, SYNC_MESSAGE_COUNT - 1);
  setFixedText(syncedMessages[index].from, sizeof(syncedMessages[index].from), csvPart(msg, 2));
  setFixedText(syncedMessages[index].text, sizeof(syncedMessages[index].text), csvPart(msg, 3));
  syncedMessages[index].valid = strlen(syncedMessages[index].from) > 0 || strlen(syncedMessages[index].text) > 0;
  saveSyncLine("/gta_sync/messages.csv", String(index) + "," + syncedMessages[index].from + "," + syncedMessages[index].text);
  needsRedraw = true;
}

void applyPhotoCommand(const String &msg) {
  int index = constrain((int)csvPartToFloat(msg, 1, 0), 0, SYNC_PHOTO_COUNT - 1);
  setFixedText(syncedPhotos[index].album, sizeof(syncedPhotos[index].album), csvPart(msg, 2));
  setFixedText(syncedPhotos[index].label, sizeof(syncedPhotos[index].label), csvPart(msg, 3));
  setFixedText(syncedPhotos[index].date, sizeof(syncedPhotos[index].date), csvPart(msg, 4));
  syncedPhotos[index].valid = strlen(syncedPhotos[index].label) > 0;
  saveSyncLine("/gta_sync/photos.csv", String(index) + "," + syncedPhotos[index].album + "," + syncedPhotos[index].label + "," + syncedPhotos[index].date);
  needsRedraw = true;
}

void handleRawCommand(String msg) {
  msg.trim();
  if (!msg.length()) {
    return;
  }
  rawCommandCount++;
  lastRawCommandMs = millis();
  setFixedText(lastRawCommand, sizeof(lastRawCommand), msg.substring(0, 62));

  if (msg.startsWith("SCREEN,")) {
    String screen = csvPart(msg, 1);
    screen.toLowerCase();
    if (screen == "map") {
      overviewMode = false;
      USBSerial.println("SCREEN map ignored zoom=close");
      return;
    } else if (screen == "clock") {
      activeScreen = SCREEN_CLOCK;
    } else if (screen == "places" || screen == "fasttravel") {
      activeScreen = SCREEN_PLACES;
    } else if (screen == "settings") {
      activeScreen = SCREEN_SETTINGS;
    }
    mapOverlayDirty = false;
    needsRedraw = true;
    USBSerial.println("SCREEN " + screen + " zoom=" + String(overviewMode ? "overview" : "close"));
    return;
  }

  if (msg.startsWith("ZOOM,")) {
    String mode = csvPart(msg, 1);
    mode.toLowerCase();
    overviewMode = mode == "overview" || mode == "big" || mode == "wide";
    mapOverlayDirty = false;
    if (activeScreen == SCREEN_MAP) {
      needsRedraw = true;
    }
    USBSerial.println("ZOOM " + String(overviewMode ? "overview" : "close"));
    return;
  }

  if (msg.startsWith("BUDDY,")) {
    String mode = csvPart(msg, 1);
    mode.toLowerCase();
    String line = csvPart(msg, 2);
    if (line.length() > 0) {
      setFixedText(buddyLine, sizeof(buddyLine), line);
    }
    if (mode == "listen") {
      buddyView = 1;
    } else if (mode == "chat") {
      buddyView = 2;
    } else if (mode == "type") {
      buddyView = 3;
    } else {
      buddyView = 0;
    }
    activeScreen = SCREEN_BUDDY;
    mapOverlayDirty = false;
    needsRedraw = true;
    USBSerial.println("BUDDY " + mode);
    return;
  }

  if (msg.startsWith("DEST,")) {
    destinationGps.valid = true;
    destinationGps.lat = csvPartToFloat(msg, 1, destinationGps.lat);
    destinationGps.lon = csvPartToFloat(msg, 2, destinationGps.lon);
    destinationGps.speed = 0.0f;
    destinationGps.heading = 0.0f;
    destinationGps.updatedMs = millis();
    setFixedText(trip.destination, sizeof(trip.destination), csvPart(msg, 3));
    routeToParking = false;
    overviewMode = false;
    mapOverlayDirty = false;
    needsRedraw = true;
    return;
  }

  if (msg.startsWith("GPS,")) {
    bool wasMap = activeScreen == SCREEN_MAP;
    bool firstLiveFix = !liveGps.valid || millis() - liveGps.updatedMs > 15000;
    gpsCommandCount++;
    liveGps.valid = true;
    liveGps.lat = csvPartToFloat(msg, 1, liveGps.lat);
    liveGps.lon = csvPartToFloat(msg, 2, liveGps.lon);
    liveGps.speed = csvPartToFloat(msg, 3, 0.0f);
    liveGps.heading = csvPartToFloat(msg, 4, liveGps.heading);
    liveGps.updatedMs = millis();
    if (!gpsAnchorValid) {
      gpsAnchorLat = liveGps.lat;
      gpsAnchorLon = liveGps.lon;
      gpsAnchorValid = true;
    }
    if (firstLiveFix) {
      overviewMode = false;
    }
    if (wasMap && !needsRedraw) {
      mapOverlayDirty = true;
    } else if (wasMap) {
      needsRedraw = true;
    }
    USBSerial.println("GPS " + String(liveGps.lat, 6) + "," + String(liveGps.lon, 6) +
                      " screen=" + String((int)activeScreen) +
                      " zoom=" + String(overviewMode ? "overview" : "close"));
    return;
  }

  if (msg.startsWith("HEAD,")) {
    headCommandCount++;
    liveGps.heading = csvPartToFloat(msg, 1, liveGps.heading);
    lastHeadCommandMs = millis();
    liveGps.updatedMs = millis();
    USBSerial.println("HEAD " + String((int)liveGps.heading));
    if (activeScreen == SCREEN_MAP && liveGps.valid) {
      mapOverlayDirty = true;
    }
    return;
  }

  if (msg.startsWith("POI,")) {
    int index = (int)csvPartToFloat(msg, 1, -1);
    if (index >= 0 && index < CUSTOM_PLACE_COUNT) {
      poiCommandCount++;
      String icon = csvPart(msg, 2);
      String label = csvPart(msg, 3);
      customPlaces[index].lat = csvPartToFloat(msg, 4, customPlaces[index].lat);
      customPlaces[index].lon = csvPartToFloat(msg, 5, customPlaces[index].lon);
      setFixedText(customPlaces[index].icon, sizeof(customPlaces[index].icon), icon);
      setFixedText(customPlaces[index].label, sizeof(customPlaces[index].label), label);
      customPlaces[index].color = colorForIcon(customPlaces[index].icon);
      customPlaces[index].valid = label.length() > 0;
      if (!gpsAnchorValid && liveGps.valid) {
        gpsAnchorLat = liveGps.lat;
        gpsAnchorLon = liveGps.lon;
        gpsAnchorValid = true;
      }
      if (activeScreen == SCREEN_MAP) {
        mapOverlayDirty = true;
      } else {
        needsRedraw = true;
      }
    }
    return;
  }

  if (msg == "ANCHOR") {
    if (liveGps.valid) {
      gpsAnchorLat = liveGps.lat;
      gpsAnchorLon = liveGps.lon;
      gpsAnchorValid = true;
    } else {
      gpsAnchorValid = false;
    }
    if (activeScreen == SCREEN_MAP) {
      mapOverlayDirty = true;
    } else {
      needsRedraw = true;
    }
    return;
  }

  if (msg == "PARK") {
    saveParkingFromCurrentFix();
    needsRedraw = true;
    return;
  }

  if (msg.startsWith("PARK,")) {
    parkingSaved = true;
    parkedGps.valid = true;
    parkedGps.lat = csvPartToFloat(msg, 1, parkedGps.lat);
    parkedGps.lon = csvPartToFloat(msg, 2, parkedGps.lon);
    parkedGps.heading = csvPartToFloat(msg, 3, 0.0f);
    parkedGps.speed = 0.0f;
    parkedGps.updatedMs = millis();
    prefs.putBool("parked", true);
    prefs.putFloat("parkLat", parkedGps.lat);
    prefs.putFloat("parkLon", parkedGps.lon);
    prefs.putFloat("parkHead", parkedGps.heading);
    if (!gpsAnchorValid) {
      gpsAnchorLat = parkedGps.lat;
      gpsAnchorLon = parkedGps.lon;
      gpsAnchorValid = true;
    }
    routeToParking = true;
    needsRedraw = true;
    return;
  }

  if (msg.startsWith("THEME,")) {
    String mode = csvPart(msg, 1);
    mode.toLowerCase();
    if (mode == "theme2") {
      activeTheme = 2;
      darkMapMode = false;
    } else if (mode == "theme3") {
      activeTheme = 3;
      darkMapMode = true;
    } else {
      activeTheme = 1;
      darkMapMode = true;
    }
    prefs.putBool("dark", darkMapMode);
    prefs.putUChar("themeId", activeTheme);
    needsRedraw = true;
    return;
  }

  if (msg.startsWith("CLOCK,")) {
    String face = csvPart(msg, 1);
    face.toLowerCase();
    if (face == "analog") {
      clockFaceId = 1;
    } else if (face == "classic") {
      clockFaceId = 2;
    } else if (face == "onehand") {
      clockFaceId = 3;
    } else if (face == "basic") {
      clockFaceId = 4;
    } else if (face == "luxury" || face == "rolex") {
      clockFaceId = 5;
    } else {
      clockFaceId = 0;
    }
    prefs.putUChar("clockFace", clockFaceId);
    needsRedraw = true;
    return;
  }

  if (msg.startsWith("MODE,")) {
    String mode = csvPart(msg, 1);
    mode.toLowerCase();
    trip.driving = mode != "walk";
    needsRedraw = true;
    return;
  }

  if (msg.startsWith("VEHICLE,")) {
    applyVehicleCommand(msg);
    needsRedraw = true;
    return;
  }

  if (msg.startsWith("TIME,")) {
    applyTimeCommand(msg);
    return;
  }

  if (msg.startsWith("PROFILE,")) {
    applyProfileCommand(msg);
    return;
  }

  if (msg.startsWith("ALARM,")) {
    applyAlarmCommand(msg);
    return;
  }

  if (msg.startsWith("WEATHER,")) {
    applyWeatherCommand(msg);
    return;
  }

  if (msg.startsWith("FITNESS,")) {
    applyFitnessCommand(msg);
    return;
  }

  if (msg.startsWith("CONTACT,")) {
    applyContactCommand(msg);
    return;
  }

  if (msg.startsWith("CALL,")) {
    applyCallCommand(msg);
    return;
  }

  if (msg.startsWith("MSG,")) {
    applyMessageCommand(msg);
    return;
  }

  if (msg.startsWith("PHOTO,")) {
    applyPhotoCommand(msg);
    return;
  }

  if (msg.startsWith("TRIP,")) {
    applyTripCommand(msg);
    needsRedraw = true;
  }
}

void rawDataCallback(uint8_t *data, int length) {
  if (length <= 0 || length > 220) {
    return;
  }
  for (int i = 0; i < length; i++) {
    char c = (char)data[i];
    if (c == '\n' || c == '\r') {
      if (rawCommandBuffer.length()) {
        USBSerial.println("RAW " + rawCommandBuffer);
        handleRawCommand(rawCommandBuffer);
        rawCommandBuffer = "";
      }
    } else if (c >= 32 && c <= 126) {
      rawCommandBuffer += c;
      if (rawCommandBuffer.length() > 180) {
        USBSerial.println("RAW " + rawCommandBuffer);
        handleRawCommand(rawCommandBuffer);
        rawCommandBuffer = "";
      }
    }
  }
}

void handleSwipe(int dx, int dy) {
  if (activeScreen == SCREEN_PLACES) {
    if (abs(dx) > 84 && abs(dx) > abs(dy) + 36) {
      activeScreen = SCREEN_MAP;
      touchSwipeConsumed = true;
      needsRedraw = true;
    } else if (abs(dy) > 34 && abs(dy) > abs(dx)) {
      int maxScroll = max(0, ((PLACE_COUNT + 2) * 47) - 330);
      placesScroll = constrain(placesScroll - dy, 0, maxScroll);
      touchScrolled = true;
      needsRedraw = true;
    }
    return;
  }

  if (touchSwipeConsumed) {
    return;
  }
  if (activeScreen == SCREEN_LAUNCHER && abs(dx) > 70 && abs(dx) > abs(dy) + 24) {
    launcherPage = dx < 0 ? 1 : 0;
    touchSwipeConsumed = true;
    needsRedraw = true;
  } else if (abs(dx) > 70 && abs(dx) > abs(dy) + 24) {
    activeScreen = nextPrimaryScreen(activeScreen, dx < 0 ? 1 : -1);
    touchSwipeConsumed = true;
    needsRedraw = true;
  } else if ((activeScreen == SCREEN_MAP || activeScreen == SCREEN_LAUNCHER) && abs(dy) > 80 && abs(dy) > abs(dx) + 24) {
    openVerticalPage(dy);
    touchSwipeConsumed = true;
    needsRedraw = true;
  } else if ((activeScreen == SCREEN_NOTIFICATIONS || activeScreen == SCREEN_MESSAGE_DETAIL ||
              activeScreen == SCREEN_PHONE || activeScreen == SCREEN_RECENTS || activeScreen == SCREEN_GALLERY ||
              activeScreen == SCREEN_CLOCK || activeScreen == SCREEN_SETTINGS ||
              activeScreen == SCREEN_WEATHER || activeScreen == SCREEN_FITNESS || activeScreen == SCREEN_CALENDAR ||
              activeScreen == SCREEN_ALARMS || activeScreen == SCREEN_ABOUT) &&
             abs(dy) > 80 && abs(dy) > abs(dx) + 24) {
    openVerticalPage(dy);
    touchSwipeConsumed = true;
  }
}

void handleTap(int x, int y) {
  if (y < 48 && x < 90) {
    activeScreen = SCREEN_LAUNCHER;
    needsRedraw = true;
    return;
  }

  if (y < 48 && x > LCD_WIDTH - 90) {
    activeScreen = nextPrimaryScreen(activeScreen, 1);
    needsRedraw = true;
    return;
  }

  if (activeScreen == SCREEN_LAUNCHER) {
    if (y >= 442 && y < 482) {
      launcherPage = x < LCD_WIDTH / 2 ? 0 : 1;
      needsRedraw = true;
      return;
    }
    if (x >= 50 && x < 182 && y >= 82 && y < 214) {
      activeScreen = launcherPage == 0 ? SCREEN_MAP : SCREEN_NOTIFICATIONS;
    } else if (x >= 228 && x < 360 && y >= 82 && y < 214) {
      activeScreen = launcherPage == 0 ? SCREEN_CLOCK : SCREEN_BUDDY;
    } else if (x >= 50 && x < 182 && y >= 268 && y < 400) {
      activeScreen = launcherPage == 0 ? SCREEN_FITNESS : SCREEN_WEATHER;
    } else if (x >= 228 && x < 360 && y >= 268 && y < 400) {
      activeScreen = launcherPage == 0 ? SCREEN_PHONE : SCREEN_SETTINGS;
    }
    needsRedraw = true;
    return;
  }

  if (activeScreen == SCREEN_MAP) {
    int mapX = 0, mapY = 0, mapW = 0, mapH = 0;
    currentRadarRect(mapX, mapY, mapW, mapH);
    if (x >= mapX && x < mapX + mapW && y >= mapY && y < mapY + mapH) {
      unsigned long now = millis();
      if (now - lastMapTapMs < 460) {
        overviewMode = !overviewMode;
        lastMapTapMs = 0;
        needsRedraw = true;
      } else {
        lastMapTapMs = now;
      }
    }
    return;
  }

  if (activeScreen == SCREEN_PLACES) {
    int row = (y + placesScroll - 124) / 47;
    if (row >= 0 && row < PLACE_COUNT) {
      selectedPlace = row;
      routeToParking = false;
      activeScreen = SCREEN_MAP;
      needsRedraw = true;
    } else if (row == PLACE_COUNT) {
      saveParkingFromCurrentFix();
      activeScreen = SCREEN_MAP;
      needsRedraw = true;
    } else if (row == PLACE_COUNT + 1) {
      routeToParking = parkingSaved;
      activeScreen = SCREEN_MECHANIC;
      needsRedraw = true;
    }
    return;
  }

  if (activeScreen == SCREEN_MECHANIC) {
    if (y > 324 && y < 398) {
      if (parkingSaved) {
        routeToParking = true;
        activeScreen = SCREEN_MAP;
      } else {
        saveParkingFromCurrentFix();
      }
      needsRedraw = true;
    }
    return;
  }

  if (activeScreen == SCREEN_BUDDY) {
    if (x >= 72 && x < 338 && y >= 304 && y < 358) {
      buddyView = 1;
      needsRedraw = true;
      return;
    }
    if (x >= 72 && x < 146 && y >= 374 && y < 406) {
      buddyView = 2;
      needsRedraw = true;
      return;
    }
    if (x >= 168 && x < 242 && y >= 374 && y < 406) {
      buddyView = 3;
      needsRedraw = true;
      return;
    }
    if (x >= 264 && x < 338 && y >= 374 && y < 406) {
      activeScreen = SCREEN_SETTINGS;
      needsRedraw = true;
      return;
    }
    buddyView = 0;
    needsRedraw = true;
    return;
  }

  if (activeScreen == SCREEN_CLOCK && y > LCD_HEIGHT - 80) {
    clockFaceId = clockFaceId >= 5 ? 0 : clockFaceId + 1;
    prefs.putUChar("clockFace", clockFaceId);
    needsRedraw = true;
    return;
  }

  if (activeScreen == SCREEN_SETTINGS && y > 120 && y < 176) {
    activeTheme = activeTheme >= 3 ? 1 : activeTheme + 1;
    darkMapMode = activeTheme != 2;
    prefs.putBool("dark", darkMapMode);
    prefs.putUChar("themeId", activeTheme);
    needsRedraw = true;
    return;
  }
}

void pollTouch() {
  bool touchDown = false;
  int x = LCD_WIDTH / 2;
  int y = LCD_HEIGHT / 2;

  if (touchReady) {
    bool interruptSeen = touch->IIC_Interrupt_Flag == true;
    int rawX = -1;
    int rawY = -1;
    int fingers = -1;
    bool readOk = ftReadTouchRaw(rawX, rawY, fingers);
    int pinState = digitalRead(TP_INT);
    bool saneCoords = rawX >= 0 && rawX < LCD_WIDTH && rawY >= 0 && rawY < LCD_HEIGHT;
    touchDown = readOk && saneCoords &&
                ((fingers > 0 && fingers < 6) || interruptSeen || pinState == LOW);
    if (touchDown) {
      x = rawX;
      y = rawY;
    }
    if (interruptSeen) {
      touch->IIC_Interrupt_Flag = false;
    }
  }

  unsigned long now = millis();
  if (touchDown && !lastTouchDown) {
    touchStartX = x;
    touchStartY = y;
    touchLastX = x;
    touchLastY = y;
    touchStartMs = now;
    touchSwipeConsumed = false;
    touchMoved = false;
    touchScrolled = false;
  } else if (touchDown && lastTouchDown) {
    int dy = y - touchLastY;
    int dx = x - touchLastX;
    int totalDx = x - touchStartX;
    int totalDy = y - touchStartY;
    if (abs(totalDx) > 18 || abs(totalDy) > 18) {
      touchMoved = true;
    }
    if (activeScreen == SCREEN_PLACES && abs(totalDy) > 20 && abs(totalDy) > abs(totalDx) && now - lastScrollDrawMs > 85) {
      int maxScroll = max(0, ((PLACE_COUNT + 2) * 47) - 330);
      placesScroll = constrain(placesScroll - dy, 0, maxScroll);
      touchScrolled = true;
      needsRedraw = true;
      lastScrollDrawMs = now;
    }
    touchLastX = x;
    touchLastY = y;
  } else if (!touchDown && lastTouchDown && now - lastTapMs > 220) {
    int totalDx = touchLastX - touchStartX;
    int totalDy = touchLastY - touchStartY;
    if (abs(totalDx) > 84 && abs(totalDx) > abs(totalDy) + 36) {
      lastTapMs = now;
      if (activeScreen == SCREEN_LAUNCHER) {
        launcherPage = totalDx < 0 ? 1 : 0;
      } else if (activeScreen == SCREEN_PLACES) {
        activeScreen = SCREEN_MAP;
      } else {
        activeScreen = nextPrimaryScreen(activeScreen, totalDx < 0 ? 1 : -1);
      }
      needsRedraw = true;
    } else if ((activeScreen == SCREEN_MAP || activeScreen == SCREEN_LAUNCHER) && abs(totalDy) > 86 && abs(totalDy) > abs(totalDx) + 28) {
      lastTapMs = now;
      openVerticalPage(totalDy);
      needsRedraw = true;
    } else if ((activeScreen == SCREEN_NOTIFICATIONS || activeScreen == SCREEN_MESSAGE_DETAIL ||
                activeScreen == SCREEN_PHONE || activeScreen == SCREEN_RECENTS || activeScreen == SCREEN_GALLERY ||
                activeScreen == SCREEN_CLOCK || activeScreen == SCREEN_SETTINGS || activeScreen == SCREEN_BUDDY ||
                activeScreen == SCREEN_WEATHER || activeScreen == SCREEN_FITNESS || activeScreen == SCREEN_CALENDAR ||
                activeScreen == SCREEN_ALARMS || activeScreen == SCREEN_ABOUT) &&
               abs(totalDy) > 86 && abs(totalDy) > abs(totalDx) + 28) {
      lastTapMs = now;
      openVerticalPage(totalDy);
      needsRedraw = true;
    } else if (!touchScrolled && abs(totalDx) < 28 && abs(totalDy) < 28 && now - touchStartMs < 700) {
      lastTapMs = now;
      handleTap(touchStartX, touchStartY);
    }
  }
  lastTouchDown = touchDown;
}

void setup() {
#if GTA_NAV_TOUCH_DIAG
  USBSerial.begin(115200);
  delay(500);
  USBSerial.println("GTA-Nav touch diagnostic booting");

  if (!gfx->begin()) {
    USBSerial.println("Display init failed");
  }
  gfx->fillScreen(COL_BLACK);

  Wire.begin(IIC_SDA, IIC_SCL, 400000);
  USBSerial.print("I2C scan:");
  String scanLine = "I2C:";
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      char found[8];
      snprintf(found, sizeof(found), " %02X", addr);
      USBSerial.print(found);
      scanLine += found;
    }
  }
  USBSerial.println();

  for (int attempt = 0; attempt < 5 && !touchReady; attempt++) {
    touchReady = touch->begin(400000);
    if (!touchReady) {
      USBSerial.printf("Touch init retry %d failed\n", attempt + 1);
      delay(120);
    }
  }
  if (touchReady) {
    touch->IIC_Interrupt_Flag = false;
    touch->IIC_Write_Device_State(
        Arduino_IIC_Touch::Device::TOUCH_POWER_MODE,
        Arduino_IIC_Touch::Device_Mode::TOUCH_POWER_ACTIVE);
    USBSerial.printf("Touch ready, device id=%ld\n", (long)touch->IIC_Read_Device_ID());
  } else {
    USBSerial.println("Touch init failed");
  }

  drawMapBitmapChunked(GTA_MAP_WATCH_CLOSE, GTA_MAP_WATCH_CLOSE_W, GTA_MAP_WATCH_CLOSE_H, 0, 62);
  gfx->fillRect(0, 0, LCD_WIDTH, 62, COL_BLACK);
  printAt(16, 12, "TOUCH TEST", COL_ROUTE, 3);
  printAt(18, 42, touchReady ? "Tap/drag screen" : "Touch init failed", touchReady ? COL_TEXT : COL_RED, 1);
  gfx->fillRect(0, LCD_HEIGHT - 78, LCD_WIDTH, 78, COL_BLACK);
  printAt(16, LCD_HEIGHT - 58, touchReady ? "Waiting for touch..." : scanLine, touchReady ? COL_MUTED : COL_TEXT, 1);
  return;
#else
  USBSerial.begin(115200);
  delay(500);
  USBSerial.println("GTA-Nav booting");
  prefs.begin("gta-nav", false);
  darkMapMode = prefs.getBool("dark", true);
  loadParking();
  loadTripDefaults();
  loadPhoneProfile();

#ifdef GFX_EXTRA_PRE_INIT
  GFX_EXTRA_PRE_INIT();
#endif

  if (!gfx->begin()) {
    USBSerial.println("Display init failed");
  }
  gfx->fillScreen(COL_BLACK);
  printAt(62, 214, "GTA-NAV", COL_ROUTE, 3);
  printAt(50, 254, "SAFE HOUSE", COL_TEXT, 2);

  Wire.begin(IIC_SDA, IIC_SCL, 400000);
  for (int attempt = 0; attempt < 5 && !touchReady; attempt++) {
    touchReady = touch->begin(400000);
    if (!touchReady) {
      USBSerial.printf("Touch init retry %d failed\n", attempt + 1);
      delay(120);
    }
  }
  if (touchReady) {
    touch->IIC_Interrupt_Flag = false;
    touch->IIC_Write_Device_State(
        Arduino_IIC_Touch::Device::TOUCH_POWER_MODE,
        Arduino_IIC_Touch::Device_Mode::TOUCH_POWER_ACTIVE);
    USBSerial.printf("Touch ready, device id=%ld\n", (long)touch->IIC_Read_Device_ID());
  } else {
    USBSerial.println("Touch init failed");
  }

  SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
  sdMounted = SD_MMC.begin("/sdcard", true);
  USBSerial.println(sdMounted ? "SD mounted" : "SD mount failed");
  ensureMapFiles();

  watch.setConnectionCallback(connectionCallback);
  watch.setNotificationCallback(notificationCallback);
  watch.setConfigurationCallback(configCallback);
  watch.setRawDataCallback(rawDataCallback);
  watch.begin();
  watch.setBattery(80);
  watch.set24Hour(false);
  USBSerial.println(watch.getAddress());

  needsRedraw = true;
#endif
}

void loop() {
#if GTA_NAV_TOUCH_DIAG
  static unsigned long lastDraw = 0;
  static int lastX = -1;
  static int lastY = -1;
  static uint32_t eventCount = 0;
  static unsigned long lastStatus = 0;
  static int lastRawX = -999;
  static int lastRawY = -999;
  static int lastFingers = -999;

  bool irq = touchReady && touch->IIC_Interrupt_Flag == true;
  int pinState = digitalRead(TP_INT);
  int rawX = -1;
  int rawY = -1;
  int fingers = -1;
  bool readOk = touchReady && ftReadTouchRaw(rawX, rawY, fingers);

  bool changed = rawX != lastRawX || rawY != lastRawY || fingers != lastFingers || irq;
  bool saneCoords = rawX >= 0 && rawX < LCD_WIDTH && rawY >= 0 && rawY < LCD_HEIGHT;
  bool touched = touchReady && readOk && ((fingers > 0 && fingers < 6) || (irq && saneCoords));
  if (touchReady && touch->IIC_Interrupt_Flag == true) {
    touch->IIC_Interrupt_Flag = false;
  }

  unsigned long now = millis();
  if ((touched || changed || now - lastStatus > 500) && now - lastDraw > 55) {
    int x = saneCoords ? rawX : LCD_WIDTH / 2;
    int y = saneCoords ? rawY : LCD_HEIGHT / 2;
    if (lastX >= 0) {
      gfx->fillCircle(lastX, lastY, 18, COL_BLACK);
    }
    if (touched) {
      gfx->fillCircle(x, y, 18, COL_ROUTE);
      gfx->drawCircle(x, y, 23, COL_TEXT);
      lastX = x;
      lastY = y;
      eventCount++;
    }
    gfx->fillRect(0, LCD_HEIGHT - 78, LCD_WIDTH, 78, COL_BLACK);
    char line[96];
    snprintf(line, sizeof(line), "OK:%d X:%d Y:%d F:%d IRQ:%d PIN:%d", readOk ? 1 : 0, rawX, rawY, fingers, irq ? 1 : 0, pinState);
    printAt(16, LCD_HEIGHT - 58, line, COL_TEXT, 1);
    snprintf(line, sizeof(line), "Touch events: %lu", (unsigned long)eventCount);
    printAt(16, LCD_HEIGHT - 34, line, COL_ROUTE, 1);
    USBSerial.printf("RAW ok=%d rawX=%d rawY=%d fingers=%d irq=%d pin=%d events=%lu\n",
                     readOk ? 1 : 0, rawX, rawY, fingers, irq ? 1 : 0, pinState, (unsigned long)eventCount);
    lastRawX = rawX;
    lastRawY = rawY;
    lastFingers = fingers;
    lastDraw = now;
    lastStatus = now;
  }
  delay(10);
#else
  watch.loop();
  pollTouch();

  unsigned long now = millis();
  pollLocalPedometer();
  if (activeScreen == SCREEN_CLOCK && now - lastClockDrawMs > 1000) {
    drawClockTick();
    lastClockDrawMs = now;
  }

  if (mapOverlayDirty && activeScreen == SCREEN_MAP && !needsRedraw) {
    drawMapLiveOverlay();
    mapOverlayDirty = false;
  }

  if (mapBlipDirty && activeScreen == SCREEN_MAP && !needsRedraw && !mapOverlayDirty) {
    drawMapBlipOverlay();
    mapBlipDirty = false;
  }

  if (needsRedraw) {
    if (activeScreen == SCREEN_MAP && lastMapDrawMs != 0 && now - lastMapDrawMs < 180) {
      delay(10);
      return;
    }
    drawScreen();
    if (activeScreen == SCREEN_MAP) {
      lastMapDrawMs = millis();
      mapOverlayDirty = false;
      mapBlipDirty = false;
    }
  }
  delay(10);
#endif
}
