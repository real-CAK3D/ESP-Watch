// Everything the UI shows, updated from phone messages. Only touched from the main loop.
#pragma once
#include <Arduino.h>

enum Maneuver : uint8_t {
  MAN_STRAIGHT = 0,
  MAN_SLIGHT_LEFT,
  MAN_LEFT,
  MAN_SHARP_LEFT,
  MAN_SLIGHT_RIGHT,
  MAN_RIGHT,
  MAN_SHARP_RIGHT,
  MAN_UTURN,
  MAN_ARRIVE,
  MAN_ROUNDABOUT,
  MAN_MERGE,
  MAN_FORK_LEFT,
  MAN_FORK_RIGHT,
  MAN_DEPART,
};

struct GpsState {
  bool valid = false;
  double lat = 0, lon = 0;
  float heading = 0;  // degrees clockwise from north
  float speed = 0;    // m/s
  float accuracy = 0;
  uint32_t rxMs = 0;
  char street[48] = "";
  char area[48] = "";
};

struct NavState {
  bool active = false;
  bool walking = false;
  uint8_t maneuver = MAN_STRAIGHT;
  float distNext = 0;    // meters to next maneuver
  float distRemain = 0;  // meters to destination
  float distTotal = 0;
  int32_t etaSec = 0;    // seconds remaining
  char instruction[64] = "";
  char street[48] = "";  // street after the maneuver
  char dest[48] = "";
  char cost[16] = "";    // e.g. "$3.40" fuel estimate, from the phone
  uint32_t rxMs = 0;
};

struct WeatherState {
  bool valid = false;
  char city[32] = "";
  int temp = 0, hi = 0, lo = 0, feels = 0, humidity = 0, wind = 0;
  int code = 0;  // WMO weather code
  int hours = 0;
  int hourH[6], hourT[6], hourCode[6];
  uint32_t rxMs = 0;
};

struct Notification {
  uint32_t id;
  char app[24];
  char title[48];
  char body[160];
  time_t when;
};

struct Place {
  char name[32];
  char kind[16];  // safehouse, mechanic, parking, work, food, shop, bank, hospital, police, gas, carwash, custom
  double lat, lon;
};

struct Settings {
  bool h24 = false;
  bool metric = false;
  uint16_t timeoutSec = 15;
  bool keepOnNav = true;
};

struct AppState {
  GpsState gps;
  NavState nav;
  WeatherState weather;
  Settings settings;
  static constexpr int MAX_NOTIF = 20;
  Notification notif[MAX_NOTIF];
  int notifCount = 0;  // newest first
  uint32_t notifSeq = 0;  // bumps on change
  static constexpr int MAX_PLACES = 40;
  Place places[MAX_PLACES];
  int placeCount = 0;
  uint32_t placesSeq = 0;
  int phoneBattery = -1;
  bool phoneCharging = false;
  bool connected = false;
  uint32_t mapSeq = 0;  // bumps when map data or route changes
};

extern AppState app;

namespace state {
void begin();  // restores places/settings from flash
void handleMessage(uint8_t type, const uint8_t *data, size_t len);
const char *weatherIcon(int wmoCode);
const char *weatherText(int wmoCode);
const char *placeIcon(const char *kind);
uint32_t placeColor(const char *kind);
void formatDistance(float meters, char *out, size_t n);
}  // namespace state
