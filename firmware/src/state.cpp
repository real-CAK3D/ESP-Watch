#include "state.h"

#include <ArduinoJson.h>
#include <LittleFS.h>

#include "hal.h"
#include "mapdata.h"
#include "ota.h"
#include "terrain.h"
#include "proto.h"
#include "ui.h"

AppState app;

static void copyStr(char *dst, size_t n, const char *src) {
  strlcpy(dst, src ? src : "", n);
}

static void savePlaces() {
  File f = LittleFS.open("/places.json", "w");
  if (!f) return;
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < app.placeCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["n"] = app.places[i].name;
    o["k"] = app.places[i].kind;
    o["lat"] = app.places[i].lat;
    o["lon"] = app.places[i].lon;
  }
  serializeJson(doc, f);
  f.close();
}

static void loadPlaces(JsonArrayConst arr) {
  app.placeCount = 0;
  for (JsonObjectConst o : arr) {
    if (app.placeCount >= AppState::MAX_PLACES) break;
    Place &p = app.places[app.placeCount++];
    copyStr(p.name, sizeof(p.name), o["n"] | "Place");
    copyStr(p.kind, sizeof(p.kind), o["k"] | "custom");
    p.lat = o["lat"] | 0.0;
    p.lon = o["lon"] | 0.0;
  }
  app.placesSeq++;
}

void state::saveSettings() {
  File f = LittleFS.open("/settings.json", "w");
  if (!f) return;
  JsonDocument doc;
  doc["h24"] = app.settings.h24;
  doc["metric"] = app.settings.metric;
  doc["timeout"] = app.settings.timeoutSec;
  doc["keepOnNav"] = app.settings.keepOnNav;
  doc["zoom"] = app.settings.radarZoom;
  doc["terrain"] = app.settings.terrain;
  doc["raise"] = app.settings.raiseToWake;
  doc["face"] = app.settings.face;
  serializeJson(doc, f);
  f.close();
}

static void applySettings(JsonObjectConst o) {
  if (o["h24"].is<bool>()) app.settings.h24 = o["h24"];
  if (o["metric"].is<bool>()) app.settings.metric = o["metric"];
  if (o["timeout"].is<int>()) app.settings.timeoutSec = constrain((int)o["timeout"], 5, 600);
  if (o["keepOnNav"].is<bool>()) app.settings.keepOnNav = o["keepOnNav"];
  if (o["bright"].is<int>()) hal::setBrightness(constrain((int)o["bright"], 10, 255));
  if (o["zoom"].is<int>()) app.settings.radarZoom = constrain((int)o["zoom"], 0, 2);
  if (o["terrain"].is<bool>()) app.settings.terrain = o["terrain"];
  if (o["raise"].is<bool>()) app.settings.raiseToWake = o["raise"];
  if (o["face"].is<int>()) app.settings.face = constrain((int)o["face"], 0, 1);
  hal::setRaiseToWake(app.settings.raiseToWake);
  app.settingsSeq++;
}

namespace state {

void begin() {
  File f = LittleFS.open("/places.json", "r");
  if (f) {
    JsonDocument doc;
    if (!deserializeJson(doc, f)) loadPlaces(doc.as<JsonArrayConst>());
    f.close();
  }
  f = LittleFS.open("/settings.json", "r");
  if (f) {
    JsonDocument doc;
    if (!deserializeJson(doc, f)) applySettings(doc.as<JsonObjectConst>());
    f.close();
  }
}

void handleMessage(uint8_t type, const uint8_t *data, size_t len) {
  if (type == MSG_MAP) {
    mapdata::load(data, len, true);
    return;
  }
  if (type == MSG_ROUTE) {
    mapdata::loadRoute(data, len);
    return;
  }
  if (type == MSG_TERRAIN) {
    terrain::load(data, len, true);
    return;
  }
  if (type == MSG_OTA_BEGIN || type == MSG_OTA_DATA || type == MSG_OTA_END) {
    ota::handle(type, data, len);
    return;
  }
  if (type == MSG_PING || type == MSG_NOTIFY_CLEAR) {
    if (type == MSG_NOTIFY_CLEAR) {
      app.notifCount = 0;
      app.notifSeq++;
    }
    return;
  }

  JsonDocument doc;
  if (len && deserializeJson(doc, (const char *)data, len)) return;

  switch (type) {
    case MSG_TIME: {
      if (doc["tz"].is<int>()) hal::setTzOffset(doc["tz"]);
      time_t t = doc["t"] | 0;
      if (t > 1700000000) hal::setUtc(t);
      break;
    }
    case MSG_GPS: {
      GpsState &g = app.gps;
      g.lat = doc["lat"] | g.lat;
      g.lon = doc["lon"] | g.lon;
      g.heading = doc["hdg"] | g.heading;
      g.speed = doc["spd"] | 0.0f;
      g.accuracy = doc["acc"] | 0.0f;
      if (doc["street"].is<const char *>()) copyStr(g.street, sizeof(g.street), doc["street"]);
      if (doc["area"].is<const char *>()) copyStr(g.area, sizeof(g.area), doc["area"]);
      g.valid = true;
      g.rxMs = millis();
      break;
    }
    case MSG_NAV: {
      NavState &n = app.nav;
      bool wasActive = n.active;
      n.active = doc["active"] | false;
      if (!n.active) {
        mapdata::clearRoute();
        if (wasActive) ui::onNavEnded();
        break;
      }
      n.walking = strcmp(doc["mode"] | "drive", "walk") == 0;
      uint8_t prevMan = n.maneuver;
      float prevDist = n.distNext;
      n.maneuver = doc["man"] | 0;
      n.distNext = doc["dist"] | 0.0f;
      n.distRemain = doc["remain"] | 0.0f;
      n.distTotal = doc["total"] | n.distRemain;
      n.etaSec = doc["eta"] | 0;
      copyStr(n.instruction, sizeof(n.instruction), doc["instr"] | "");
      copyStr(n.street, sizeof(n.street), doc["street"] | "");
      copyStr(n.dest, sizeof(n.dest), doc["dest"] | "");
      copyStr(n.cost, sizeof(n.cost), doc["cost"] | "");
      n.rxMs = millis();
      if (!wasActive) ui::onNavStarted();
      // heads-up when a turn is coming close
      const float alertAt = n.walking ? 30.0f : 150.0f;
      if (n.distNext <= alertAt && (prevDist > alertAt || prevMan != n.maneuver)) ui::onTurnApproaching();
      break;
    }
    case MSG_WEATHER: {
      WeatherState &w = app.weather;
      copyStr(w.city, sizeof(w.city), doc["city"] | "");
      w.temp = doc["t"] | 0;
      w.hi = doc["hi"] | 0;
      w.lo = doc["lo"] | 0;
      w.feels = doc["feels"] | w.temp;
      w.humidity = doc["hum"] | 0;
      w.wind = doc["wind"] | 0;
      w.code = doc["code"] | 0;
      w.hours = 0;
      for (JsonArrayConst h : doc["hours"].as<JsonArrayConst>()) {
        if (w.hours >= 6) break;
        w.hourH[w.hours] = h[0];
        w.hourT[w.hours] = h[1];
        w.hourCode[w.hours] = h[2];
        w.hours++;
      }
      w.valid = true;
      w.rxMs = millis();
      break;
    }
    case MSG_NOTIFY: {
      int n = min(app.notifCount, AppState::MAX_NOTIF - 1);
      memmove(&app.notif[1], &app.notif[0], sizeof(Notification) * n);
      Notification &nt = app.notif[0];
      nt.id = doc["id"] | 0;
      copyStr(nt.app, sizeof(nt.app), doc["app"] | "");
      copyStr(nt.title, sizeof(nt.title), doc["title"] | "");
      copyStr(nt.body, sizeof(nt.body), doc["body"] | "");
      nt.when = time(nullptr);
      app.notifCount = n + 1;
      app.notifSeq++;
      ui::onNotification(nt);
      break;
    }
    case MSG_PLACES:
      loadPlaces(doc.as<JsonArrayConst>());
      savePlaces();
      break;
    case MSG_SETTINGS:
      applySettings(doc.as<JsonObjectConst>());
      state::saveSettings();
      break;
    case MSG_PHONE:
      app.phoneBattery = doc["bat"] | -1;
      app.phoneCharging = doc["chg"] | false;
      break;
  }
}

const char *weatherIcon(int c) {
  if (c == 0 || c == 1) return "\xEF\x86\x85";               // sun f185
  if (c == 2) return "\xEF\x9B\x84";                          // cloud-sun f6c4
  if (c == 3) return "\xEF\x83\x82";                          // cloud f0c2
  if (c == 45 || c == 48) return "\xEF\x9D\x9F";              // smog f75f
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return "\xEF\x8B\x9C";  // snowflake f2dc
  if (c >= 95) return "\xEF\x83\xA7";                         // bolt f0e7
  if (c >= 51) return "\xEF\x9D\x80";                         // cloud-rain f740
  return "\xEF\x83\x82";
}

const char *weatherText(int c) {
  if (c == 0) return "Clear";
  if (c == 1) return "Mostly clear";
  if (c == 2) return "Partly cloudy";
  if (c == 3) return "Overcast";
  if (c == 45 || c == 48) return "Fog";
  if (c >= 51 && c <= 57) return "Drizzle";
  if (c >= 61 && c <= 67) return "Rain";
  if (c >= 71 && c <= 77) return "Snow";
  if (c >= 80 && c <= 82) return "Showers";
  if (c == 85 || c == 86) return "Snow showers";
  if (c >= 95) return "Thunderstorm";
  return "";
}

// FontAwesome glyphs (UTF-8) for GTA-style place blips.
const char *placeIcon(const char *k) {
  if (!strcmp(k, "safehouse")) return "\xEF\x80\x95";  // home f015
  if (!strcmp(k, "mechanic")) return "\xEF\x82\xAD";   // wrench f0ad
  if (!strcmp(k, "parking")) return "\xEF\x86\xB9";    // car f1b9
  if (!strcmp(k, "carwash")) return "\xEF\x81\x83";    // tint f043 (Pay 'n' Spray)
  if (!strcmp(k, "food")) return "\xEF\x8B\xA7";       // utensils f2e7 -> fallback below if missing
  if (!strcmp(k, "gas")) return "\xEF\x81\xAD";        // fire f06d
  if (!strcmp(k, "bank")) return "\xEF\x85\x95";       // dollar f155
  if (!strcmp(k, "hospital")) return "\xEF\x80\x84";   // heart f004
  if (!strcmp(k, "police")) return "\xEF\x80\x85";     // star f005
  if (!strcmp(k, "work")) return "\xEF\x82\xB1";       // briefcase f0b1
  if (!strcmp(k, "shop")) return "\xEF\x81\xBA";       // shopping-cart f07a
  if (!strcmp(k, "dest")) return "\xEF\x84\x9E";       // flag-checkered f11e
  return "\xEF\x80\x85";                                // star
}

uint32_t placeColor(const char *k) {
  if (!strcmp(k, "safehouse")) return 0x5DA9E9;
  if (!strcmp(k, "mechanic")) return 0x9CD08F;
  if (!strcmp(k, "parking")) return 0xF0C850;
  if (!strcmp(k, "carwash")) return 0xE06C4C;
  if (!strcmp(k, "food")) return 0xF0A040;
  if (!strcmp(k, "gas")) return 0xE8903A;
  if (!strcmp(k, "bank")) return 0x72CC72;
  if (!strcmp(k, "hospital")) return 0xE05A5A;
  if (!strcmp(k, "police")) return 0x4C8CC9;
  if (!strcmp(k, "dest")) return 0xB456E0;
  return 0xFFFFFF;
}

void formatDistance(float m, char *out, size_t n) {
  if (app.settings.metric) {
    if (m < 1000) snprintf(out, n, "%d m", (int)(roundf(m / 10) * 10));
    else snprintf(out, n, "%.1f km", m / 1000.0f);
  } else {
    float ft = m * 3.28084f;
    if (ft < 1000) snprintf(out, n, "%d ft", (int)(roundf(ft / 10) * 10));
    else snprintf(out, n, "%.1f mi", m / 1609.34f);
  }
}

}  // namespace state
