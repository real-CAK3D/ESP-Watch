# Watch ⇄ phone protocol

Source of truth: `firmware/src/proto.h` (mirrored in `android/.../Protocol.kt`).

## Transport

BLE GATT service `6f9a0001-5a7c-4d2b-9e1f-47a5c0de0001`
- RX `…0002` phone → watch, write (with response)
- TX `…0003` watch → phone, notify

Every packet: `[type u8][flags u8][payload…]`, flags bit0 = first chunk, bit1 = last chunk.
Messages bigger than MTU−5 are split and reassembled. The USB console accepts the same
messages (`m <type> <seq> <total> <adler32> <base64>` chunks, ACKed) for PC testing.

## Phone → watch

| Type | Name | Payload |
|---|---|---|
| 0x01 | TIME | `{"t": unix_utc, "tz": offset_s}` |
| 0x02 | WEATHER | `{"city","t","hi","lo","feels","hum","wind","code"(WMO),"hours":[[hour,temp,code]…]}` |
| 0x03 | GPS | `{"lat","lon","hdg","spd","acc","street","area"}` ~1–2 Hz |
| 0x04 | MAP | binary, below |
| 0x05 | NAV | `{"active","mode":"drive|walk","man","dist","remain","total","eta","instr","street","dest","cost"}` or `{"active":false}` |
| 0x06 | ROUTE | `i32 lat0e7, i32 lon0e7, u16 n, n × (i16 x, i16 y)` (0.5 m units) |
| 0x07 | NOTIFY | `{"id","app","title","body"}` |
| 0x08 | PLACES | `[{"n","k","lat","lon"}…]` k = safehouse, mechanic, parking, carwash, food, gas, bank, hospital, police, work, shop, custom |
| 0x09 | SETTINGS | `{"h24","metric","timeout","bright","zoom":0-2,"terrain","raise","face":0-1,"keepOnNav"}` |
| 0x0A | PHONE | `{"bat","chg"}` |
| 0x0B | PING | empty → watch replies with TELEMETRY |
| 0x0C | NOTIFY_CLEAR | empty |
| 0x0D | TERRAIN | binary hill-shade grid, below |
| 0x10 | OTA_BEGIN | `{"size": bytes, "ver": "x.y.z"}` → watch erases the spare app slot, answers OTA_STATUS `ready` |
| 0x11 | OTA_DATA | `u32 offset` + up to 8 KB of the app image; each block answered with OTA_STATUS `ack` + bytes written |
| 0x12 | OTA_END | empty → watch verifies the image, switches boot slot, answers `done`, restarts |

Maneuver codes (`man`): 0 straight, 1 slight left, 2 left, 3 sharp left, 4 slight right,
5 right, 6 sharp right, 7 U-turn, 8 arrive, 9 roundabout, 10 merge, 11 fork left,
12 fork right, 13 depart.

### MAP blob

```
'G' 'M' u8 version=1 u8 0  i32 lat0*1e7  i32 lon0*1e7  u16 featureCount
feature: u8 kind, u8 0, u16 npts, npts × (i16 x, i16 y)   // 0.5 m units, x east, y north
```
Kinds: 1 motorway, 2 primary, 3 secondary, 4 street, 5 service, 6 path, 7 rail,
20 water (area), 21 river (line), 22 park (area), 23 beach (area).
Projection: `x = (lon−lon0)·111320·cos(lat0)`, `y = (lat−lat0)·110574`. The phone sends a new
map (≈1.6 km radius) whenever you move more than 700 m from the current map's anchor.

### TERRAIN blob

```
i32 lat0*1e7  i32 lon0*1e7  u16 n  u16 cell_dm   then n*n u8 shade (row 0 = north)
```
Hill shading from AWS Terrain Tiles (terrarium, zoom 13), sun from the north-west at 45°, 2.5×
vertical exaggeration; 128 = flat. The phone sends a 128×128 grid of 25 m cells with every new map.

## Watch → phone

| Type | Name | Payload |
|---|---|---|
| 0x81 | TELEMETRY | `{"fw","bat","mv","chg","usb","steps","up","heap","psram","scr","page","mtu"}` every 15 s |
| 0x82 | EVENT | `{"e":"hello"}`, `open_map`, `navigate` + `"i"` (place index), `save_parking`, `find_phone`, `nav_stop`, `battery_low` + `"i"` (percent), `charged` |
| 0x83 | LOG | text |
| 0x84 | OTA_STATUS | `{"st":"ready|ack|done|error","off":bytes_written,"err":"..."}` |
