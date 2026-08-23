# GTA-Nav Android Companion

Native Android companion for the GTA-Nav watch.

What it does:

- Scans for the BLE device named `GTA-Nav`.
- Connects to the Chronos/Nordic UART service:
  - Service: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
  - RX/write: `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
- Requests phone location.
- Sends watch commands:
  - `ANCHOR`
  - `GPS,<lat>,<lon>,<speed_mph>,<heading>`
- Has a `Test Move` button to prove BLE writes without waiting for live GPS movement.

Why this exists:

Chrome Web Bluetooth, Chronos, and the watch all compete for one BLE connection. A native app gives GTA-Nav one owner for GPS and watch commands.

Build:

1. Install Android Studio, or install JDK 17 plus Android SDK command-line tools.
2. Open this folder in Android Studio:
   `gta-nav-android`
3. Let Gradle sync.
4. Build `app-debug.apk`.
5. Install it on the Android phone.

Use:

1. Close Chronos while testing direct GPS movement.
2. Open GTA-Nav.
3. Tap `Connect Watch`.
4. Tap `Test Move`; the watch should receive fake GPS.
5. Tap `Start GPS`; the watch should receive live phone GPS.
