# GTA-Nav Bridge Protocol

GTA-Nav keeps Chronos for normal watch features and accepts custom raw BLE text
commands for live GPS and parking.

BLE service:

- Service UUID: `6e400001-b5a3-f393-e0a9-e50e24dcca9e`
- Write/RX UUID: `6e400002-b5a3-f393-e0a9-e50e24dcca9e`
- Notify/TX UUID: `6e400003-b5a3-f393-e0a9-e50e24dcca9e`

Commands:

```text
GPS,<lat>,<lon>,<speed_mph>,<heading_degrees>
PARK
PARK,<lat>,<lon>,<heading_degrees>
THEME,dark
THEME,light
MODE,drive
MODE,walk
VEHICLE,<name>,<tank_gallons>,<mpg>,<fuel_type>,<gas_price_per_gallon>
TRIP,<destination>,<drive_or_walk>,<route_miles>,<toll_cost>,<gas_price_per_gallon>
```

Examples:

```text
GPS,40.712776,-74.005974,22.4,91
PARK,40.712800,-74.006120,270
THEME,dark
VEHICLE,4Runner,23.0,18.5,Regular,3.49
TRIP,Safe_House,drive,12.4,1.75,3.49
TRIP,Garage,walk,1.2,0,3.49
```

Notes:

- `PARK` without coordinates saves the current watch-side GPS fix.
- `PARK,<lat>,<lon>` is better when the phone knows the exact parking fix.
- Chronos app navigation still works for turn-by-turn text/icons.
- The custom bridge is for true continuous phone GPS.
- Driving trip cost is `(route_miles / mpg * gas_price_per_gallon) + toll_cost`.
- Walking mode ignores gas and toll cost on the watch display.
