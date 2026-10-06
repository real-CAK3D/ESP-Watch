#include "ui.h"

#include <lvgl.h>

#include "hal.h"
#include "mapdata.h"
#include "ota.h"
#include "phone.h"
#include "state.h"
#include "ui_common.h"

using namespace theme;

namespace {

constexpr int W = hal::W, H = hal::H;

lv_obj_t *tv;
lv_obj_t *tiles[7];
ui::Page current = ui::PAGE_FACE;
uint32_t screenOffAtMs = 0;

// ---------------- watch face ----------------
lv_obj_t *faceDigital, *faceAnalog;
lv_obj_t *fDate, *fTime, *fAmPm, *fStatus, *fStatusIcon, *fNav;
lv_obj_t *fArc[3], *fArcIcon[3], *fArcVal[3];
lv_obj_t *aHour, *aMin, *aSec, *aDate, *aStatus;
lv_point_precise_t aHourPts[2], aMinPts[2], aSecPts[2];
bool analogFace = false;
lv_obj_t *faceTopBar;

// ---------------- weather ----------------
lv_obj_t *wCity, *wIcon, *wTemp, *wDesc, *wHiLo, *wFeels, *wHum, *wWind, *wEmpty;
lv_obj_t *wHourCol[5], *wHourT[5], *wHourI[5], *wHourV[5];

// ---------------- places ----------------
lv_obj_t *pList, *pStopBtn;
uint32_t pSeqShown = 0;
lv_obj_t *pDistLbls[AppState::MAX_PLACES];

// ---------------- activity ----------------
lv_obj_t *acArc, *acSteps, *acDist, *acCal, *acWatchBat, *acPhoneBat;

// ---------------- quick settings ----------------
lv_obj_t *qConn, *qBattery, *qInfo, *qBright;

// ---------------- notifications ----------------
lv_obj_t *nList;
uint32_t nSeqShown = 0;

// ---------------- toast ----------------
lv_obj_t *toastBox, *toastLbl;
lv_timer_t *toastTimer;
lv_obj_t *popup, *popupApp, *popupTitle, *popupBody;
lv_timer_t *popupTimer;

const char *const PAGE_NAMES[] = {"weather", "face", "map", "places", "activity", "quick", "notifications"};

void styleTile(lv_obj_t *t) {
  lv_obj_set_style_bg_color(t, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
  lv_obj_set_scrollbar_mode(t, LV_SCROLLBAR_MODE_OFF);
}

lv_obj_t *pageTitle(lv_obj_t *parent, const char *text, uint32_t color = DIM) {
  lv_obj_t *l = uiLabel(parent, &font_md, color, text);
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 26);
  return l;
}

lv_obj_t *arcRing(lv_obj_t *parent, int size, int width, uint32_t color) {
  lv_obj_t *a = lv_arc_create(parent);
  lv_obj_set_size(a, size, size);
  lv_arc_set_rotation(a, 270);
  lv_arc_set_bg_angles(a, 0, 360);
  lv_arc_set_range(a, 0, 100);
  lv_arc_set_value(a, 0);
  lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
  lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(a, width, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, width, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, lv_color_hex(color), LV_PART_MAIN);
  lv_obj_set_style_arc_opa(a, LV_OPA_20, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
  return a;
}

const char *batteryIcon(int pct) {
  if (pct >= 90) return ICON_BATTERY_FULL;
  if (pct >= 65) return ICON_BATTERY_3;
  if (pct >= 40) return ICON_BATTERY_2;
  if (pct >= 15) return ICON_BATTERY_1;
  return ICON_BATTERY_0;
}

// ======================= FACE =======================
void applyFace(bool analog) {
  analogFace = analog;
  if (analogFace) {
    lv_obj_add_flag(faceDigital, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(faceAnalog, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_remove_flag(faceDigital, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(faceAnalog, LV_OBJ_FLAG_HIDDEN);
  }
}

void toggleFace(lv_event_t *) {
  applyFace(!analogFace);
  app.settings.face = analogFace ? 1 : 0;
  state::saveSettings();
}

void createFace(lv_obj_t *t) {
  lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(t, toggleFace, LV_EVENT_LONG_PRESSED, nullptr);

  // --- digital (default) ---
  faceDigital = uiBox(t);
  lv_obj_set_size(faceDigital, W, H);
  lv_obj_add_flag(faceDigital, LV_OBJ_FLAG_EVENT_BUBBLE);

  fDate = uiLabel(faceDigital, &font_lg, GREEN, "");
  lv_obj_set_style_text_letter_space(fDate, 2, 0);
  lv_obj_align(fDate, LV_ALIGN_TOP_MID, 0, 34);

  fTime = uiLabel(faceDigital, &font_clock, TEXT, "--:--");
  lv_obj_set_style_text_letter_space(fTime, -2, 0);
  lv_obj_align(fTime, LV_ALIGN_TOP_MID, 0, 92);
  fAmPm = uiLabel(faceDigital, &font_md, DIM, "");
  lv_obj_align_to(fAmPm, fTime, LV_ALIGN_OUT_RIGHT_TOP, 0, 34);

  const uint32_t colors[3] = {GREEN, BLUE, YELLOW};
  const char *iconsTxt[3] = {ICON_BATTERY_FULL, ICON_SHOE, ICON_SUN};
  for (int i = 0; i < 3; i++) {
    fArc[i] = arcRing(faceDigital, 112, 9, colors[i]);
    lv_obj_align(fArc[i], LV_ALIGN_TOP_MID, (i - 1) * 124, 290);
    fArcIcon[i] = uiLabel(fArc[i], &font_sm, colors[i], iconsTxt[i]);
    lv_obj_align(fArcIcon[i], LV_ALIGN_CENTER, 0, -17);
    fArcVal[i] = uiLabel(fArc[i], &font_md, TEXT, "--");
    lv_obj_align(fArcVal[i], LV_ALIGN_CENTER, 0, 14);
  }

  lv_obj_t *statusRow = uiBox(faceDigital);
  lv_obj_set_size(statusRow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(statusRow, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(statusRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(statusRow, 8, 0);
  lv_obj_align(statusRow, LV_ALIGN_TOP_MID, 0, 410);
  fStatusIcon = uiLabel(statusRow, &font_sm, BLUE, ICON_BT);
  fStatus = uiLabel(statusRow, &font_sm, DIM, "Phone offline");

  fNav = uiLabel(faceDigital, &font_md, PURPLE, "");
  lv_obj_set_width(fNav, W - 80);
  lv_obj_set_style_text_align(fNav, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(fNav, LV_LABEL_LONG_DOT);
  lv_obj_align(fNav, LV_ALIGN_TOP_MID, 0, 440);

  // --- analog ---
  faceAnalog = uiBox(t);
  lv_obj_set_size(faceAnalog, W, H);
  lv_obj_add_flag(faceAnalog, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(faceAnalog, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_t *scale = lv_scale_create(faceAnalog);
  lv_obj_set_size(scale, 380, 380);
  lv_obj_center(scale);
  lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
  lv_scale_set_range(scale, 0, 60);
  lv_scale_set_angle_range(scale, 360);
  lv_scale_set_rotation(scale, 270);
  lv_scale_set_total_tick_count(scale, 61);
  lv_scale_set_major_tick_every(scale, 5);
  lv_scale_set_label_show(scale, false);
  lv_obj_set_style_length(scale, 8, LV_PART_ITEMS);
  lv_obj_set_style_length(scale, 20, LV_PART_INDICATOR);
  lv_obj_set_style_line_color(scale, lv_color_hex(0x555555), LV_PART_ITEMS);
  lv_obj_set_style_line_color(scale, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_line_width(scale, 2, LV_PART_ITEMS);
  lv_obj_set_style_line_width(scale, 5, LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(scale, 0, LV_PART_MAIN);
  lv_obj_remove_flag(scale, LV_OBJ_FLAG_CLICKABLE);
  static const char *nums[] = {"12", "3", "6", "9"};
  for (int i = 0; i < 4; i++) {
    lv_obj_t *n = uiLabel(faceAnalog, &font_lg, TEXT, nums[i]);
    float a = i * (float)M_PI / 2;
    lv_obj_align(n, LV_ALIGN_CENTER, (int)(sinf(a) * 140), (int)(-cosf(a) * 140));
  }
  aDate = uiLabel(faceAnalog, &font_md, GREEN, "");
  lv_obj_align(aDate, LV_ALIGN_CENTER, 0, 70);
  aStatus = uiLabel(faceAnalog, &font_sm, DIM, "");
  lv_obj_align(aStatus, LV_ALIGN_CENTER, 0, -70);
  auto hand = [&](lv_point_precise_t *pts, int width, uint32_t color) {
    lv_obj_t *l = lv_line_create(faceAnalog);
    lv_obj_set_size(l, W, H);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, lv_color_hex(color), 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    pts[0] = {W / 2, H / 2};
    pts[1] = {W / 2, H / 2};
    lv_line_set_points(l, pts, 2);
    return l;
  };
  aHour = hand(aHourPts, 12, TEXT);
  aMin = hand(aMinPts, 8, TEXT);
  aSec = hand(aSecPts, 3, GREEN);
  lv_obj_t *hub = uiBox(faceAnalog);
  lv_obj_set_size(hub, 18, 18);
  lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(hub, lv_color_hex(GREEN), 0);
  lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);
  lv_obj_center(hub);
}

void setHand(lv_obj_t *line, lv_point_precise_t *pts, float frac, float len) {
  float a = frac * 2 * (float)M_PI;
  pts[0] = {W / 2 - sinf(a) * 18, H / 2 + cosf(a) * 18};
  pts[1] = {W / 2 + sinf(a) * len, H / 2 - cosf(a) * len};
  lv_line_set_points(line, pts, 2);
}

void updateFace() {
  static int lastSec = -1;
  struct tm t;
  hal::localTime(&t);
  if (t.tm_sec == lastSec) return;
  lastSec = t.tm_sec;

  static const char *DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const char *MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  char buf[48];
  bool valid = hal::timeValid();

  if (!analogFace) {
    snprintf(buf, sizeof(buf), "%s %d %s", DAYS[t.tm_wday], t.tm_mday, MONTHS[t.tm_mon]);
    lv_label_set_text(fDate, valid ? buf : "SYNC WITH PHONE");
    ui::formatClock(buf, sizeof(buf), false);
    lv_label_set_text(fTime, buf);
    lv_label_set_text(fAmPm, app.settings.h24 ? "" : (t.tm_hour < 12 ? "AM" : "PM"));
    lv_obj_align_to(fAmPm, fTime, LV_ALIGN_OUT_RIGHT_TOP, 2, 36);

    int bat = hal::batteryPercent();
    lv_arc_set_value(fArc[0], bat < 0 ? 0 : bat);
    lv_label_set_text(fArcIcon[0], hal::charging() ? ICON_CHARGE : batteryIcon(bat));
    snprintf(buf, sizeof(buf), bat < 0 ? "USB" : "%d%%", bat);
    lv_label_set_text(fArcVal[0], buf);

    uint32_t steps = hal::stepsToday();
    lv_arc_set_value(fArc[1], min<uint32_t>(100, steps * 100 / 8000));
    if (steps >= 10000) snprintf(buf, sizeof(buf), "%.1fk", steps / 1000.0f);
    else snprintf(buf, sizeof(buf), "%lu", (unsigned long)steps);
    lv_label_set_text(fArcVal[1], buf);

    if (app.weather.valid) {
      lv_label_set_text(fArcIcon[2], state::weatherIcon(app.weather.code));
      snprintf(buf, sizeof(buf), "%d\xC2\xB0", app.weather.temp);
      lv_label_set_text(fArcVal[2], buf);
      int span = max(1, app.weather.hi - app.weather.lo);
      lv_arc_set_value(fArc[2], constrain((app.weather.temp - app.weather.lo) * 100 / span, 0, 100));
    }

    if (app.connected) {
      lv_obj_set_style_text_color(fStatusIcon, lv_color_hex(BLUE), 0);
      if (app.phoneBattery >= 0) snprintf(buf, sizeof(buf), "Phone %d%%", app.phoneBattery);
      else strcpy(buf, "Phone connected");
      lv_label_set_text(fStatus, buf);
    } else {
      lv_obj_set_style_text_color(fStatusIcon, lv_color_hex(0x555555), 0);
      lv_label_set_text(fStatus, "Phone offline");
    }
    if (app.nav.active) {
      char d[16];
      state::formatDistance(app.nav.distNext, d, sizeof(d));
      snprintf(buf, sizeof(buf), ICON_ROUTE "  %s  %s", d, app.nav.street[0] ? app.nav.street : app.nav.dest);
      lv_label_set_text(fNav, buf);
    } else {
      lv_label_set_text(fNav, app.notifCount ? ICON_BELL : "");
      lv_obj_set_style_text_color(fNav, lv_color_hex(app.notifCount ? YELLOW : PURPLE), 0);
    }
  } else {
    float s = t.tm_sec / 60.0f, m = (t.tm_min + s) / 60.0f, h = ((t.tm_hour % 12) + m) / 12.0f;
    setHand(aHour, aHourPts, h, 100);
    setHand(aMin, aMinPts, m, 150);
    setHand(aSec, aSecPts, s, 165);
    snprintf(buf, sizeof(buf), "%s %d", DAYS[t.tm_wday], t.tm_mday);
    lv_label_set_text(aDate, buf);
    int bat = hal::batteryPercent();
    snprintf(buf, sizeof(buf), "%s %d%%   " ICON_SHOE " %lu", batteryIcon(bat), max(bat, 0), (unsigned long)hal::stepsToday());
    lv_label_set_text(aStatus, buf);
  }
}

// ======================= WEATHER =======================
void createWeather(lv_obj_t *t) {
  wCity = pageTitle(t, "Weather");
  wIcon = uiLabel(t, &font_xl, YELLOW, ICON_SUN);
  lv_obj_align(wIcon, LV_ALIGN_TOP_MID, -80, 96);
  wTemp = uiLabel(t, &font_clock_sm, TEXT, "--");
  lv_obj_align(wTemp, LV_ALIGN_TOP_MID, 40, 64);
  wDesc = uiLabel(t, &font_lg, TEXT, "");
  lv_obj_align(wDesc, LV_ALIGN_TOP_MID, 0, 172);
  wHiLo = uiLabel(t, &font_md, DIM, "");
  lv_obj_align(wHiLo, LV_ALIGN_TOP_MID, 0, 212);

  lv_obj_t *stats = uiCard(t, W - 60, 64);
  lv_obj_align(stats, LV_ALIGN_TOP_MID, 0, 250);
  lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(stats, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  wFeels = uiLabel(stats, &font_sm, TEXT, ICON_THERMO " --");
  wHum = uiLabel(stats, &font_sm, TEXT, ICON_TINT " --");
  wWind = uiLabel(stats, &font_sm, TEXT, ICON_WIND " --");

  lv_obj_t *hours = uiCard(t, W - 60, 140);
  lv_obj_align(hours, LV_ALIGN_TOP_MID, 0, 326);
  lv_obj_set_flex_flow(hours, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(hours, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (int i = 0; i < 5; i++) {
    wHourCol[i] = uiBox(hours);
    lv_obj_set_size(wHourCol[i], 62, 120);
    lv_obj_set_flex_flow(wHourCol[i], LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wHourCol[i], LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    wHourT[i] = uiLabel(wHourCol[i], &font_sm, DIM, "--");
    wHourI[i] = uiLabel(wHourCol[i], &font_md, YELLOW, "");
    wHourV[i] = uiLabel(wHourCol[i], &font_md, TEXT, "");
  }
  wEmpty = uiLabel(t, &font_sm, DIM, "Weather comes from the phone app");
  lv_obj_align(wEmpty, LV_ALIGN_BOTTOM_MID, 0, -20);
}

void updateWeather() {
  static uint32_t shown = 0;
  const WeatherState &w = app.weather;
  if (!w.valid || w.rxMs == shown) return;
  shown = w.rxMs;
  char b[48];
  lv_label_set_text(wCity, w.city[0] ? w.city : "Weather");
  lv_label_set_text(wIcon, state::weatherIcon(w.code));
  snprintf(b, sizeof(b), "%d\xC2\xB0", w.temp);
  lv_label_set_text(wTemp, b);
  lv_label_set_text(wDesc, state::weatherText(w.code));
  snprintf(b, sizeof(b), "H %d\xC2\xB0   L %d\xC2\xB0", w.hi, w.lo);
  lv_label_set_text(wHiLo, b);
  snprintf(b, sizeof(b), ICON_THERMO " %d\xC2\xB0", w.feels);
  lv_label_set_text(wFeels, b);
  snprintf(b, sizeof(b), ICON_TINT " %d%%", w.humidity);
  lv_label_set_text(wHum, b);
  snprintf(b, sizeof(b), ICON_WIND " %d %s", w.wind, app.settings.metric ? "km/h" : "mph");
  lv_label_set_text(wWind, b);
  for (int i = 0; i < 5; i++) {
    if (i < w.hours) {
      int h = w.hourH[i];
      if (app.settings.h24) snprintf(b, sizeof(b), "%02d:00", h);
      else snprintf(b, sizeof(b), "%d%s", h % 12 == 0 ? 12 : h % 12, h < 12 ? "am" : "pm");
      lv_label_set_text(wHourT[i], b);
      lv_label_set_text(wHourI[i], state::weatherIcon(w.hourCode[i]));
      snprintf(b, sizeof(b), "%d\xC2\xB0", w.hourT[i]);
      lv_label_set_text(wHourV[i], b);
    }
  }
  lv_obj_add_flag(wEmpty, LV_OBJ_FLAG_HIDDEN);
}

// ======================= PLACES =======================
void onPlaceClicked(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  phone::sendEvent("navigate", idx);
  char msg[64];
  snprintf(msg, sizeof(msg), app.connected ? "Route to %s" : "Phone not connected", app.places[idx].name);
  ui::toast(msg, ICON_ROUTE);
  if (app.connected) ui::goPage(ui::PAGE_MAP);
}

void onParkHere(lv_event_t *) {
  phone::sendEvent("save_parking");
  ui::toast(app.connected ? "Parking spot saved" : "Phone not connected", ICON_PARKING);
}
void onStopNav(lv_event_t *) {
  phone::sendEvent("nav_stop");
  ui::toast("Navigation stopped", ICON_STOP);
}
void onFindPhone(lv_event_t *) {
  phone::sendEvent("find_phone");
  ui::toast(app.connected ? "Ringing your phone" : "Phone not connected", ICON_MOBILE);
}

lv_obj_t *pillButton(lv_obj_t *parent, const char *text, uint32_t color, lv_event_cb_t cb) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, LV_SIZE_CONTENT, 58);
  lv_obj_set_style_pad_hor(b, 18, 0);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(CARD_HI), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(0x3A4145), LV_STATE_PRESSED);
  lv_obj_t *l = uiLabel(b, &font_md, color, text);
  lv_obj_center(l);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  return b;
}

void createPlaces(lv_obj_t *t) {
  pList = lv_obj_create(t);
  lv_obj_remove_style_all(pList);
  lv_obj_set_size(pList, W, H);
  lv_obj_set_flex_flow(pList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(pList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(pList, 24, 0);
  lv_obj_set_style_pad_bottom(pList, 60, 0);
  lv_obj_set_style_pad_row(pList, 10, 0);
  lv_obj_set_scroll_dir(pList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(pList, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(pList, LV_OBJ_FLAG_SCROLL_MOMENTUM);
}

void rebuildPlaces() {
  lv_obj_clean(pList);
  uiLabel(pList, &font_md, DIM, "Places");

  lv_obj_t *row = uiBox(pList);
  lv_obj_set_size(row, W - 40, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 10, 0);
  pillButton(row, ICON_PARKING " Park", YELLOW, onParkHere);
  pillButton(row, ICON_MOBILE " Find", BLUE, onFindPhone);
  pStopBtn = pillButton(row, ICON_STOP " Stop", RED, onStopNav);
  if (!app.nav.active) lv_obj_add_flag(pStopBtn, LV_OBJ_FLAG_HIDDEN);

  if (app.placeCount == 0) {
    lv_obj_t *l = uiLabel(pList, &font_sm, DIM, "Add Safe Houses, your Mechanic\nand other spots in the phone app.");
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  }
  for (int i = 0; i < app.placeCount; i++) {
    const Place &p = app.places[i];
    lv_obj_t *b = lv_button_create(pList);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, W - 40, 84);
    lv_obj_set_style_radius(b, 26, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(CARD), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(CARD_HI), LV_STATE_PRESSED);
    lv_obj_add_flag(b, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_add_event_cb(b, onPlaceClicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    lv_obj_t *ic = uiBox(b);
    lv_obj_set_size(ic, 54, 54);
    lv_obj_set_style_radius(ic, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ic, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ic, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ic, 2, 0);
    lv_obj_set_style_border_color(ic, lv_color_hex(state::placeColor(p.kind)), 0);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_t *il = uiLabel(ic, &font_md, state::placeColor(p.kind), state::placeIcon(p.kind));
    lv_obj_center(il);

    lv_obj_t *nm = uiLabel(b, &font_md, TEXT, p.name);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    lv_obj_set_width(nm, W - 40 - 90 - 20);
    lv_obj_set_pos(nm, 82, 14);
    pDistLbls[i] = uiLabel(b, &font_sm, DIM, "");
    lv_obj_set_pos(pDistLbls[i], 82, 46);
  }
  pSeqShown = app.placesSeq;
}

void updatePlaces() {
  static bool navShown = false;
  if (pSeqShown != app.placesSeq || navShown != app.nav.active) {
    navShown = app.nav.active;
    rebuildPlaces();
  }
  static uint32_t last = 0;
  if (millis() - last < 2000) return;
  last = millis();
  for (int i = 0; i < app.placeCount; i++) {
    if (!app.gps.valid) {
      lv_label_set_text(pDistLbls[i], "Tap to set GPS route");
      continue;
    }
    // equirectangular is plenty at these distances
    double dLat = (app.places[i].lat - app.gps.lat) * 110574.0;
    double dLon = (app.places[i].lon - app.gps.lon) * 111320.0 * cos(app.gps.lat * DEG_TO_RAD);
    char d[16], b[40];
    state::formatDistance((float)sqrt(dLat * dLat + dLon * dLon), d, sizeof(d));
    snprintf(b, sizeof(b), "%s away", d);
    lv_label_set_text(pDistLbls[i], b);
  }
}

// ======================= ACTIVITY =======================
void createActivity(lv_obj_t *t) {
  pageTitle(t, "Activity");
  acArc = arcRing(t, 250, 22, BLUE);
  lv_obj_align(acArc, LV_ALIGN_TOP_MID, 0, 70);
  lv_obj_t *ic = uiLabel(acArc, &font_lg, BLUE, ICON_SHOE);
  lv_obj_align(ic, LV_ALIGN_CENTER, 0, -48);
  acSteps = uiLabel(acArc, &font_xl, TEXT, "0");
  lv_obj_align(acSteps, LV_ALIGN_CENTER, 0, 4);
  lv_obj_t *goal = uiLabel(acArc, &font_sm, DIM, "of 8,000 steps");
  lv_obj_align(goal, LV_ALIGN_CENTER, 0, 44);

  lv_obj_t *row = uiCard(t, W - 60, 70);
  lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 336);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  acDist = uiLabel(row, &font_md, TEXT, ICON_WALK " 0");
  acCal = uiLabel(row, &font_md, TEXT, ICON_FIRE " 0");

  lv_obj_t *row2 = uiCard(t, W - 60, 70);
  lv_obj_align(row2, LV_ALIGN_TOP_MID, 0, 414);
  lv_obj_set_flex_flow(row2, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row2, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  acWatchBat = uiLabel(row2, &font_md, GREEN, "");
  acPhoneBat = uiLabel(row2, &font_md, BLUE, "");
}

void updateActivity() {
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  uint32_t s = hal::stepsToday();
  char b[48];
  lv_arc_set_value(acArc, min<uint32_t>(100, s * 100 / 8000));
  snprintf(b, sizeof(b), "%lu", (unsigned long)s);
  lv_label_set_text(acSteps, b);
  char d[16];
  state::formatDistance(s * 0.76f, d, sizeof(d));
  snprintf(b, sizeof(b), ICON_WALK " %s", d);
  lv_label_set_text(acDist, b);
  snprintf(b, sizeof(b), ICON_FIRE " %lu kcal", (unsigned long)(s * 0.04f));
  lv_label_set_text(acCal, b);
  int wb = hal::batteryPercent();
  snprintf(b, sizeof(b), "%s %d%%", hal::charging() ? ICON_CHARGE : batteryIcon(wb), max(wb, 0));
  lv_label_set_text(acWatchBat, b);
  if (app.phoneBattery >= 0) snprintf(b, sizeof(b), ICON_MOBILE " %d%%", app.phoneBattery);
  else snprintf(b, sizeof(b), ICON_MOBILE " --");
  lv_label_set_text(acPhoneBat, b);
}

// ======================= QUICK SETTINGS =======================
void onBright(lv_event_t *e) { hal::setBrightness((uint8_t)lv_slider_get_value((lv_obj_t *)lv_event_get_target(e))); }

lv_obj_t *toggleRow(lv_obj_t *parent, const char *text, bool on, lv_event_cb_t cb) {
  lv_obj_t *row = uiCard(parent, W - 60, 66);
  lv_obj_t *l = uiLabel(row, &font_md, TEXT, text);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 22, 0);
  lv_obj_t *sw = lv_switch_create(row);
  lv_obj_set_size(sw, 70, 38);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -16, 0);
  lv_obj_set_style_bg_color(sw, lv_color_hex(GREEN), LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, nullptr);
  return row;
}

void createQuick(lv_obj_t *t) {
  lv_obj_t *col = lv_obj_create(t);
  lv_obj_remove_style_all(col);
  lv_obj_set_size(col, W, H);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(col, 26, 0);
  lv_obj_set_style_pad_bottom(col, 40, 0);
  lv_obj_set_style_pad_row(col, 10, 0);
  lv_obj_set_scroll_dir(col, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(col, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t *status = uiBox(col);
  lv_obj_set_size(status, W - 60, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(status, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  qConn = uiLabel(status, &font_md, BLUE, "");
  qBattery = uiLabel(status, &font_md, GREEN, "");

  lv_obj_t *bc = uiCard(col, W - 60, 80);
  lv_obj_t *sunIc = uiLabel(bc, &font_md, YELLOW, ICON_SUN);
  lv_obj_align(sunIc, LV_ALIGN_LEFT_MID, 20, 0);
  qBright = lv_slider_create(bc);
  lv_obj_set_size(qBright, W - 60 - 100, 16);
  lv_obj_align(qBright, LV_ALIGN_LEFT_MID, 64, 0);
  lv_slider_set_range(qBright, 20, 255);
  lv_slider_set_value(qBright, hal::brightness(), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(qBright, lv_color_hex(YELLOW), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(qBright, lv_color_white(), LV_PART_KNOB);
  lv_obj_add_event_cb(qBright, onBright, LV_EVENT_VALUE_CHANGED, nullptr);

  toggleRow(col, "Raise to wake", app.settings.raiseToWake, [](lv_event_t *e) {
    app.settings.raiseToWake = lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED);
    hal::setRaiseToWake(app.settings.raiseToWake);
    state::saveSettings();
  });
  toggleRow(col, "Map stays on", app.settings.keepOnNav, [](lv_event_t *e) {
    app.settings.keepOnNav = lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED);
    state::saveSettings();
  });
  toggleRow(col, "Terrain shading", app.settings.terrain, [](lv_event_t *e) {
    app.settings.terrain = lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED);
    app.mapSeq++;
    state::saveSettings();
  });
  toggleRow(col, "24-hour clock", app.settings.h24, [](lv_event_t *e) {
    app.settings.h24 = lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED);
    state::saveSettings();
  });
  toggleRow(col, "Metric units", app.settings.metric, [](lv_event_t *e) {
    app.settings.metric = lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED);
    state::saveSettings();
  });

  qInfo = uiLabel(col, &font_sm, DIM, "");
  lv_obj_set_style_text_align(qInfo, LV_TEXT_ALIGN_CENTER, 0);
}

void updateQuick() {
  static uint32_t last = 0;
  if (millis() - last < 1000) return;
  last = millis();
  char b[160];
  lv_label_set_text(qConn, app.connected ? ICON_BT " Connected" : ICON_BT " Offline");
  lv_obj_set_style_text_color(qConn, lv_color_hex(app.connected ? BLUE : 0x666666), 0);
  int wb = hal::batteryPercent();
  snprintf(b, sizeof(b), "%s%s %d%%", hal::powerSaverOn() ? "Saver  " : "", hal::charging() ? ICON_CHARGE : batteryIcon(wb),
           max(wb, 0));
  lv_label_set_text(qBattery, b);
  lv_obj_set_style_text_color(qBattery, lv_color_hex(hal::powerSaverOn() ? YELLOW : GREEN), 0);
  uint32_t up = millis() / 1000;
  snprintf(b, sizeof(b), "GTA-Watch %s  \xE2\x80\xA2  up %luh %02lum\n%d mV  \xE2\x80\xA2  map %s", FW_VERSION,
           (unsigned long)(up / 3600), (unsigned long)(up / 60 % 60), hal::batteryMillivolts(),
           mapdata::hasMap() ? "loaded" : "none");
  lv_label_set_text(qInfo, b);
}

// ======================= NOTIFICATIONS =======================
void createNotif(lv_obj_t *t) {
  nList = lv_obj_create(t);
  lv_obj_remove_style_all(nList);
  lv_obj_set_size(nList, W, H);
  lv_obj_set_flex_flow(nList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(nList, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(nList, 24, 0);
  lv_obj_set_style_pad_bottom(nList, 50, 0);
  lv_obj_set_style_pad_row(nList, 10, 0);
  lv_obj_set_scroll_dir(nList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(nList, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(nList, LV_OBJ_FLAG_SCROLL_MOMENTUM);
}

void rebuildNotif() {
  lv_obj_clean(nList);
  uiLabel(nList, &font_md, DIM, "Notifications");
  if (app.notifCount == 0) {
    lv_obj_t *l = uiLabel(nList, &font_md, 0x555555, ICON_BELL "\nAll caught up");
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(l, 120, 0);
  }
  for (int i = 0; i < app.notifCount; i++) {
    const Notification &n = app.notif[i];
    lv_obj_t *c = uiCard(nList, W - 40, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(c, 18, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(c, 4, 0);
    char hdr[48];
    struct tm t;
    time_t lt = n.when + hal::tzOffset();
    gmtime_r(&lt, &t);
    if (app.settings.h24) snprintf(hdr, sizeof(hdr), "%s  \xE2\x80\xA2  %02d:%02d", n.app, t.tm_hour, t.tm_min);
    else snprintf(hdr, sizeof(hdr), "%s  \xE2\x80\xA2  %d:%02d", n.app, t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12, t.tm_min);
    uiLabel(c, &font_sm, YELLOW, hdr);
    lv_obj_t *ti = uiLabel(c, &font_md, TEXT, n.title);
    lv_label_set_long_mode(ti, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ti, W - 76);
    lv_obj_t *bd = uiLabel(c, &font_sm, 0xC8C8C8, n.body);
    lv_label_set_long_mode(bd, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(bd, W - 76);
  }
  if (app.notifCount) {
    lv_obj_t *b = pillButton(nList, ICON_TRASH " Clear all", RED, [](lv_event_t *) {
      app.notifCount = 0;
      app.notifSeq++;
    });
    (void)b;
  }
  nSeqShown = app.notifSeq;
}

// ======================= TOAST / POPUP =======================
void createOverlays() {
  lv_obj_t *top = lv_layer_top();
  toastBox = uiBox(top);
  lv_obj_set_size(toastBox, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(toastBox, lv_color_hex(0x202427), 0);
  lv_obj_set_style_bg_opa(toastBox, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(toastBox, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(toastBox, 22, 0);
  lv_obj_set_style_pad_ver(toastBox, 12, 0);
  lv_obj_set_style_shadow_width(toastBox, 24, 0);
  lv_obj_set_style_shadow_color(toastBox, lv_color_black(), 0);
  toastLbl = uiLabel(toastBox, &font_md, TEXT, "");
  lv_obj_align(toastBox, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_add_flag(toastBox, LV_OBJ_FLAG_HIDDEN);
  toastTimer = lv_timer_create([](lv_timer_t *) { lv_obj_add_flag(toastBox, LV_OBJ_FLAG_HIDDEN); }, 2200, nullptr);
  lv_timer_pause(toastTimer);
  lv_timer_set_repeat_count(toastTimer, -1);

  popup = uiCard(top, W - 36, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(popup, lv_color_hex(0x1C2023), 0);
  lv_obj_set_style_border_color(popup, lv_color_hex(YELLOW), 0);
  lv_obj_set_style_border_width(popup, 2, 0);
  lv_obj_set_style_pad_all(popup, 20, 0);
  lv_obj_set_flex_flow(popup, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(popup, 6, 0);
  lv_obj_align(popup, LV_ALIGN_TOP_MID, 0, 24);
  lv_obj_add_flag(popup, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(popup, [](lv_event_t *) { lv_obj_add_flag(popup, LV_OBJ_FLAG_HIDDEN); }, LV_EVENT_CLICKED, nullptr);
  popupApp = uiLabel(popup, &font_sm, YELLOW, "");
  popupTitle = uiLabel(popup, &font_lg, TEXT, "");
  lv_label_set_long_mode(popupTitle, LV_LABEL_LONG_DOT);
  lv_obj_set_width(popupTitle, W - 76);
  popupBody = uiLabel(popup, &font_md, 0xD0D0D0, "");
  lv_label_set_long_mode(popupBody, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(popupBody, W - 76);
  lv_obj_set_style_max_height(popupBody, 190, 0);
  lv_obj_add_flag(popup, LV_OBJ_FLAG_HIDDEN);
  popupTimer = lv_timer_create([](lv_timer_t *) { lv_obj_add_flag(popup, LV_OBJ_FLAG_HIDDEN); }, 6000, nullptr);
  lv_timer_pause(popupTimer);
}

// ======================= CHARGING / UPDATE / SPLASH =======================
lv_obj_t *chgScreen, *chgArc, *chgPct, *chgState, *chgEta;
uint32_t chgShownAt = 0;

lv_obj_t *fullscreenLayer() {
  lv_obj_t *o = uiBox(lv_layer_top());
  lv_obj_set_size(o, W, H);
  lv_obj_set_style_bg_color(o, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  return o;
}

void createCharging() {
  chgScreen = fullscreenLayer();
  lv_obj_add_flag(chgScreen, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(chgScreen, [](lv_event_t *) { lv_obj_add_flag(chgScreen, LV_OBJ_FLAG_HIDDEN); }, LV_EVENT_CLICKED, nullptr);
  chgArc = arcRing(chgScreen, 300, 22, GREEN);
  lv_obj_align(chgArc, LV_ALIGN_CENTER, 0, -30);
  lv_obj_t *bolt = uiLabel(chgArc, &font_lg, GREEN, ICON_CHARGE);
  lv_obj_align(bolt, LV_ALIGN_CENTER, 0, -62);
  chgPct = uiLabel(chgArc, &font_clock_sm, TEXT, "--");
  lv_obj_align(chgPct, LV_ALIGN_CENTER, 0, 10);
  chgState = uiLabel(chgScreen, &font_lg, TEXT, "Charging");
  lv_obj_align(chgState, LV_ALIGN_BOTTOM_MID, 0, -78);
  chgEta = uiLabel(chgScreen, &font_md, DIM, "");
  lv_obj_align(chgEta, LV_ALIGN_BOTTOM_MID, 0, -44);
}

void updateCharging() {
  if (lv_obj_has_flag(chgScreen, LV_OBJ_FLAG_HIDDEN)) return;
  if (millis() - chgShownAt > 6000 || !hal::usbPowered()) {
    lv_obj_add_flag(chgScreen, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  static uint32_t last = 0;
  if (millis() - last < 200) return;
  last = millis();
  int pct = max(0, hal::batteryPercent());
  char b[48];
  snprintf(b, sizeof(b), "%d%%", pct);
  lv_label_set_text(chgPct, b);
  // the ring "breathes" while charging, like Wear OS / watchOS
  float pulse = 0.5f + 0.5f * sinf(millis() / 300.0f);
  lv_arc_set_value(chgArc, pct);
  lv_obj_set_style_arc_opa(chgArc, hal::chargeComplete() ? LV_OPA_COVER : (lv_opa_t)(150 + 105 * pulse), LV_PART_INDICATOR);
  lv_label_set_text(chgState, hal::chargeComplete() ? "Fully charged" : "Charging");
  int eta = hal::chargeEtaMinutes();
  if (hal::chargeComplete()) b[0] = 0;
  else if (eta < 0) strcpy(b, "Estimating time to full");
  else if (eta < 60) snprintf(b, sizeof(b), "Full in %d min", eta);
  else snprintf(b, sizeof(b), "Full in %dh %02dm", eta / 60, eta % 60);
  lv_label_set_text(chgEta, b);
}

void showCharging() {
  chgShownAt = millis();
  lv_obj_remove_flag(chgScreen, LV_OBJ_FLAG_HIDDEN);
  hal::screenOn(true);
  hal::noteActivity();
  updateCharging();
}

lv_obj_t *otaScreen, *otaArc, *otaPct, *otaTitle, *otaSub;

void createOta() {
  otaScreen = fullscreenLayer();
  otaTitle = uiLabel(otaScreen, &font_lg, TEXT, "Updating watch");
  lv_obj_align(otaTitle, LV_ALIGN_TOP_MID, 0, 60);
  otaArc = arcRing(otaScreen, 250, 18, BLUE);
  lv_obj_align(otaArc, LV_ALIGN_CENTER, 0, 10);
  otaPct = uiLabel(otaArc, &font_xl, TEXT, "0%");
  lv_obj_center(otaPct);
  otaSub = uiLabel(otaScreen, &font_md, DIM, "Keep the phone nearby");
  lv_obj_align(otaSub, LV_ALIGN_BOTTOM_MID, 0, -50);
}

void createSplash() {
  // GTA-style loading screen while everything else starts up
  lv_obj_t *sp = fullscreenLayer();
  lv_obj_remove_flag(sp, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t *top = uiLabel(sp, &font_md, TEXT, "GRAND THEFT AUTO");
  lv_obj_set_style_text_letter_space(top, 4, 0);
  lv_obj_align(top, LV_ALIGN_CENTER, 0, -70);
  lv_obj_t *big = uiLabel(sp, &font_xl, TEXT, "W A T C H");
  lv_obj_align(big, LV_ALIGN_CENTER, 0, -14);
  lv_obj_t *ver = uiLabel(sp, &font_sm, GREEN, "v" FW_VERSION);
  lv_obj_align(ver, LV_ALIGN_CENTER, 0, 34);
  lv_obj_t *spin = lv_spinner_create(sp);
  lv_obj_set_size(spin, 34, 34);
  lv_obj_align(spin, LV_ALIGN_BOTTOM_RIGHT, -40, -40);
  lv_obj_set_style_arc_width(spin, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_width(spin, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(spin, lv_color_white(), LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(spin, LV_OPA_20, LV_PART_MAIN);
  lv_obj_t *tip = uiLabel(sp, &font_sm, DIM, "Tip: tap the minimap to open the\nbig map on your phone.");
  lv_obj_set_style_text_align(tip, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(tip, LV_ALIGN_BOTTOM_MID, 0, -100);
  lv_obj_fade_out(sp, 400, 1600);
  lv_obj_delete_delayed(sp, 2100);
}

// low battery alerts + power saver, charge-complete notice
void batteryLogic() {
  if (hal::takeUsbPlugged()) showCharging();
  if (hal::takeChargeFull()) {
    phone::sendEvent("charged");
    if (hal::screenIsOn()) ui::toast("Fully charged", ICON_CHARGE);
  }
  static uint32_t last = 0;
  if (millis() - last < 5000) return;
  last = millis();
  int pct = hal::batteryPercent();
  bool onUsb = hal::usbPowered();
  hal::setPowerSaver(!onUsb && pct >= 0 && pct <= 15);
  static int warnedAt = 101;
  if (onUsb || pct < 0) {
    warnedAt = 101;
    return;
  }
  for (int level : {20, 10, 5}) {
    if (pct <= level && warnedAt > level) {
      warnedAt = level;
      phone::sendEvent("battery_low", pct);
      Notification n = {};
      strlcpy(n.app, "Battery", sizeof(n.app));
      snprintf(n.title, sizeof(n.title), "%d%% battery left", pct);
      strlcpy(n.body, level <= 15 ? "Power saver is on: dimmer screen, shorter timeout. Charge soon." : "Charge your watch soon.",
              sizeof(n.body));
      ui::onNotification(n);
      break;
    }
  }
}

void onTileChanged(lv_event_t *) {
  lv_obj_t *act = lv_tileview_get_tile_active(tv);
  for (int i = 0; i < 7; i++)
    if (tiles[i] == act) current = (ui::Page)i;
  hal::noteActivity();
}

void screenPowerLogic() {
  if (hal::takePowerPress()) {
    if (hal::screenIsOn()) {
      hal::screenOn(false);
      screenOffAtMs = millis();
    } else {
      hal::screenOn(true);
    }
  }
  if (hal::takeBootPress()) {
    if (!hal::screenIsOn()) {
      hal::screenOn(true);
    } else {
      ui::goPage(current == ui::PAGE_FACE ? ui::PAGE_MAP : ui::PAGE_FACE);
    }
    hal::noteActivity();
  }

  bool keepOn = app.settings.keepOnNav && app.nav.active && current == ui::PAGE_MAP;
  uint32_t timeout = app.settings.timeoutSec * 1000UL;
  if (hal::powerSaverOn()) timeout = min<uint32_t>(timeout, 8000);
  if (ota::active()) keepOn = true;
  if (!lv_obj_has_flag(chgScreen, LV_OBJ_FLAG_HIDDEN)) keepOn = true;
  if (hal::screenIsOn() && !keepOn && hal::idleMs() > timeout) {
    hal::screenOn(false);
    screenOffAtMs = millis();
  }

  static bool wasOn = true;
  bool on = hal::screenIsOn();
  if (on && !wasOn) {
    // coming back: navigation wins, otherwise return home after a longer sleep
    if (app.nav.active) ui::goPage(ui::PAGE_MAP, false);
    else if (millis() - screenOffAtMs > 60000 && current != ui::PAGE_FACE) ui::goPage(ui::PAGE_FACE, false);
  }
  wasOn = on;
}

}  // namespace

namespace ui {

const char *pageName() { return PAGE_NAMES[current]; }

void formatClock(char *out, size_t n, bool withSeconds) {
  if (!hal::timeValid()) {
    snprintf(out, n, "--:--");
    return;
  }
  struct tm t;
  hal::localTime(&t);
  int h = t.tm_hour;
  if (!app.settings.h24) h = h % 12 == 0 ? 12 : h % 12;
  if (withSeconds) snprintf(out, n, app.settings.h24 ? "%02d:%02d:%02d" : "%d:%02d:%02d", h, t.tm_min, t.tm_sec);
  else snprintf(out, n, app.settings.h24 ? "%02d:%02d" : "%d:%02d", h, t.tm_min);
}

void begin() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_text_color(scr, lv_color_white(), 0);

  tv = lv_tileview_create(scr);
  lv_obj_set_size(tv, W, H);
  lv_obj_set_style_bg_color(tv, lv_color_black(), 0);
  lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(tv, onTileChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  tiles[PAGE_WEATHER] = lv_tileview_add_tile(tv, 0, 1, LV_DIR_RIGHT);
  tiles[PAGE_FACE] = lv_tileview_add_tile(tv, 1, 1, LV_DIR_ALL);
  tiles[PAGE_MAP] = lv_tileview_add_tile(tv, 2, 1, LV_DIR_HOR);
  tiles[PAGE_PLACES] = lv_tileview_add_tile(tv, 3, 1, LV_DIR_HOR);
  tiles[PAGE_ACTIVITY] = lv_tileview_add_tile(tv, 4, 1, LV_DIR_LEFT);
  tiles[PAGE_QUICK] = lv_tileview_add_tile(tv, 1, 0, LV_DIR_BOTTOM);
  tiles[PAGE_NOTIF] = lv_tileview_add_tile(tv, 1, 2, LV_DIR_TOP);
  for (auto t : tiles) styleTile(t);

  createFace(tiles[PAGE_FACE]);
  createWeather(tiles[PAGE_WEATHER]);
  ui_map::create(tiles[PAGE_MAP]);
  createPlaces(tiles[PAGE_PLACES]);
  rebuildPlaces();
  createActivity(tiles[PAGE_ACTIVITY]);
  createQuick(tiles[PAGE_QUICK]);
  createNotif(tiles[PAGE_NOTIF]);
  rebuildNotif();
  createOverlays();
  createCharging();
  createOta();
  applyFace(app.settings.face == 1);
  createSplash();

  goPage(PAGE_FACE, false);
}

void otaProgress(int pct, const char *text) {
  if (pct < 0) {
    lv_label_set_text(otaTitle, "Update failed");
    lv_label_set_text(otaSub, text ? text : "");
    lv_obj_fade_out(otaScreen, 300, 4000);
    return;
  }
  lv_obj_remove_flag(otaScreen, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_opa(otaScreen, LV_OPA_COVER, 0);
  lv_label_set_text(otaTitle, "Updating watch");
  if (text && text[0]) {
    char b[48];
    if (pct == 0) snprintf(b, sizeof(b), "Installing v%s", text);
    else strlcpy(b, text, sizeof(b));
    lv_label_set_text(otaSub, b);
  }
  lv_arc_set_value(otaArc, pct);
  char p[8];
  snprintf(p, sizeof(p), "%d%%", pct);
  lv_label_set_text(otaPct, p);
}

void goPage(Page p, bool animate) {
  static const int8_t COL[] = {0, 1, 2, 3, 4, 1, 1};
  static const int8_t ROW[] = {1, 1, 1, 1, 1, 0, 2};
  lv_tileview_set_tile_by_index(tv, COL[p], ROW[p], animate ? LV_ANIM_ON : LV_ANIM_OFF);
  current = p;
  hal::noteActivity();
}

void loop() {
  batteryLogic();
  screenPowerLogic();
  if (!hal::screenIsOn()) return;
  static uint32_t settingsShown = 0;
  if (settingsShown != app.settingsSeq) {
    settingsShown = app.settingsSeq;
    applyFace(app.settings.face == 1);
  }
  updateCharging();
  updateFace();
  switch (current) {
    case PAGE_MAP: ui_map::tick(); break;
    case PAGE_WEATHER: updateWeather(); break;
    case PAGE_PLACES: updatePlaces(); break;
    case PAGE_ACTIVITY: updateActivity(); break;
    case PAGE_QUICK: updateQuick(); break;
    case PAGE_NOTIF:
      if (nSeqShown != app.notifSeq) rebuildNotif();
      break;
    default: break;
  }
  // keep neighbours fresh so swipes land on current data
  if (current == PAGE_FACE) {
    updateWeather();
    if (pSeqShown != app.placesSeq) rebuildPlaces();
    if (nSeqShown != app.notifSeq) rebuildNotif();
  }
}

void toast(const char *text, const char *icon) {
  char b[96];
  if (icon) snprintf(b, sizeof(b), "%s  %s", icon, text);
  else strlcpy(b, text, sizeof(b));
  lv_label_set_text(toastLbl, b);
  lv_obj_remove_flag(toastBox, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(toastBox, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_timer_reset(toastTimer);
  lv_timer_resume(toastTimer);
  hal::screenOn(true);
  hal::noteActivity();
}

void onNavStarted() {
  hal::screenOn(true);
  goPage(PAGE_MAP);
  toast("GPS route set", ICON_ROUTE);
}
void onNavEnded() {}
void onTurnApproaching() {
  hal::screenOn(true);
  hal::noteActivity();
  if (current != PAGE_MAP) goPage(PAGE_MAP);
}

void onNotification(const Notification &n) {
  lv_label_set_text(popupApp, n.app);
  lv_label_set_text(popupTitle, n.title);
  lv_label_set_text(popupBody, n.body);
  lv_obj_remove_flag(popup, LV_OBJ_FLAG_HIDDEN);
  lv_timer_reset(popupTimer);
  lv_timer_resume(popupTimer);
  hal::screenOn(true);
  hal::noteActivity();
}

void onConnectionChanged(bool connected) {
  if (hal::screenIsOn()) toast(connected ? "Phone connected" : "Phone disconnected", ICON_BT);
}

}  // namespace ui
