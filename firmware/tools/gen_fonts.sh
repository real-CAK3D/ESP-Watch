#!/usr/bin/env bash
# Regenerates src/fonts/*.c from the TTFs in tools/fonts (Barlow, OFL) + FontAwesome 5 icons.
set -e
cd "$(dirname "$0")"
OUT=../src/fonts
# LVGL's built-in LV_SYMBOL_* set plus extra icons used by the watch UI.
LV_SYMS=61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62189,62212,62810,63426,63650
EXTRA=0xf004,0xf005,0xf015,0xf0ad,0xf095,0xf075,0xf0e0,0xf11e,0xf0c2,0xf185,0xf186,0xf6c4,0xf73d,0xf2dc,0xf0e7,0xf75f,0xf72e,0xf043,0xf554,0xf1b9,0xf54b,0xf279,0xf124,0xf4d7,0xf540,0xf06d,0xf05b,0xf3cd,0xf2c9,0xf017,0xf073,0xf1eb,0xf6c3,0xf740,0xf7ab,0xf0f2,0xf2b9,0xf1e6,0xf155,0xf0b1,0xf07a,0xf2e7,0xf3c5,0xf057,0xf00d,0xf058,0xf1da,0xf140
conv() { # name ttf size range
  npx -y lv_font_conv@1.5.3 --no-compress --no-prefilter --bpp 4 --format lvgl --lv-include lvgl.h \
    --size "$3" --font "fonts/$2" -r "$4" ${5:+--font fonts/FontAwesome5.woff -r $LV_SYMS,$EXTRA} -o "$OUT/$1.c" --lv-font-name "$1"
}
conv font_clock BarlowCondensed-SemiBold.ttf 176 0x2D,0x30-0x3A
conv font_clock_sm BarlowCondensed-SemiBold.ttf 96 0x30-0x3A,0x20,0x2D,0x41,0x4D,0x50,0xB0
conv font_xl Barlow-SemiBold.ttf 44 0x20-0x7E,0xB0,0x2022 icons
conv font_lg Barlow-SemiBold.ttf 32 0x20-0x7E,0xB0,0x2022 icons
conv font_md Barlow-SemiBold.ttf 24 0x20-0x7E,0xB0,0x2022,0x2013,0x2019 icons
conv font_sm Barlow-Medium.ttf 19 0x20-0x7E,0xB0,0x2022,0x2013,0x2019 icons
echo fonts ok
