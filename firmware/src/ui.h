// Watch UI: page layout, gestures, screen power, toasts.
//
// Page grid (swipe between neighbours, like Wear OS / watchOS):
//
//                    [Quick settings]
//   [Weather]  <->     [Watch face]    <->  [GTA minimap]  <->  [Places]  <->  [Activity]
//                    [Notifications]
//
// Buttons: BOOT = home (watch face) / wake, PWR = screen on/off.
#pragma once
#include <Arduino.h>

struct Notification;

namespace ui {
void begin();
void loop();
const char *pageName();

enum Page { PAGE_WEATHER, PAGE_FACE, PAGE_MAP, PAGE_PLACES, PAGE_ACTIVITY, PAGE_QUICK, PAGE_NOTIF };
void goPage(Page p, bool animate = true);

void toast(const char *text, const char *icon = nullptr);
void formatClock(char *out, size_t n, bool withSeconds);

// events from state/phone
void onNavStarted();
void onNavEnded();
void onTurnApproaching();
void onNotification(const Notification &n);
void onConnectionChanged(bool connected);
}  // namespace ui
