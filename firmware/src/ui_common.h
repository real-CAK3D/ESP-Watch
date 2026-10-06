// Shared look & feel for the watch pages.
#pragma once
#include <lvgl.h>

LV_FONT_DECLARE(font_clock);
LV_FONT_DECLARE(font_clock_sm);
LV_FONT_DECLARE(font_xl);
LV_FONT_DECLARE(font_lg);
LV_FONT_DECLARE(font_md);
LV_FONT_DECLARE(font_sm);

namespace theme {
constexpr uint32_t BG = 0x000000;
constexpr uint32_t CARD = 0x16191B;
constexpr uint32_t CARD_HI = 0x23282B;
constexpr uint32_t TEXT = 0xFFFFFF;
constexpr uint32_t DIM = 0x98A2A6;
constexpr uint32_t GREEN = 0x72CC72;  // GTA money / health green
constexpr uint32_t BLUE = 0x5DADE2;   // armor blue
constexpr uint32_t YELLOW = 0xF0C850;
constexpr uint32_t PURPLE = 0xA64CF2; // GPS route
constexpr uint32_t RED = 0xE05A5A;
constexpr uint32_t ORANGE = 0xF09A3E;
}  // namespace theme

// FontAwesome glyphs present in the generated fonts
#define ICON_BT "\xEF\x8A\x93"
#define ICON_BATTERY_FULL "\xEF\x89\x80"
#define ICON_BATTERY_3 "\xEF\x89\x81"
#define ICON_BATTERY_2 "\xEF\x89\x82"
#define ICON_BATTERY_1 "\xEF\x89\x83"
#define ICON_BATTERY_0 "\xEF\x89\x84"
#define ICON_CHARGE "\xEF\x83\xA7"
#define ICON_WALK "\xEF\x95\x94"
#define ICON_SHOE "\xEF\x95\x8B"
#define ICON_FIRE "\xEF\x81\xAD"
#define ICON_ROUTE "\xEF\x93\x97"
#define ICON_MAP "\xEF\x89\xB9"
#define ICON_CAR "\xEF\x86\xB9"
#define ICON_PHONE "\xEF\x82\x95"
#define ICON_MOBILE "\xEF\x8F\x8D"
#define ICON_BELL "\xEF\x83\xB3"
#define ICON_SUN "\xEF\x86\x85"
#define ICON_TINT "\xEF\x81\x83"
#define ICON_WIND "\xEF\x9C\xAE"
#define ICON_THERMO "\xEF\x8B\x89"
#define ICON_TRASH "\xEF\x8B\xAD"
#define ICON_CLOSE "\xEF\x80\x8D"
#define ICON_CHECK "\xEF\x80\x8C"
#define ICON_CROSSHAIR "\xEF\x81\x9B"
#define ICON_PARKING "\xEF\x95\x80"
#define ICON_STOP "\xEF\x81\x97"
#define ICON_CLOCK "\xEF\x80\x97"
#define ICON_HEART "\xEF\x80\x84"
#define ICON_SETTINGS "\xEF\x80\x93"
#define ICON_COMMENT "\xEF\x81\xB5"
#define ICON_LOCATION "\xEF\x84\xA4"

// Plain container with no theme padding/border/scrollbars.
inline lv_obj_t *uiBox(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  return o;
}

inline lv_obj_t *uiLabel(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text = "") {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, text);
  return l;
}

inline lv_obj_t *uiCard(lv_obj_t *parent, int w, int h) {
  lv_obj_t *c = uiBox(parent);
  lv_obj_set_size(c, w, h);
  lv_obj_set_style_bg_color(c, lv_color_hex(theme::CARD), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c, 28, 0);
  return c;
}

namespace ui_map {
void create(lv_obj_t *tile);
void tick();
}
