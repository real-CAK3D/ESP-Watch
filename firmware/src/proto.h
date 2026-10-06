// Watch <-> phone protocol. Mirrored in the Android app (Protocol.kt) and docs/PROTOCOL.md.
//
// Transport: one BLE GATT service with two characteristics.
//   RX (phone -> watch): write / write-without-response
//   TX (watch -> phone): notify
// Every BLE packet is:  [type:u8][flags:u8][payload...]
//   flags bit0 = first chunk of a message, bit1 = last chunk.
// Messages larger than one packet are split; the receiver concatenates payloads
// from FIRST to LAST. Small messages have flags = FIRST|LAST.
// The USB serial console accepts the same messages ("msg <type> <base64>") so
// the whole pipeline can be tested from a PC without the phone.
#pragma once
#include <stdint.h>

#define GTAW_SERVICE_UUID "6f9a0001-5a7c-4d2b-9e1f-47a5c0de0001"
#define GTAW_RX_UUID      "6f9a0002-5a7c-4d2b-9e1f-47a5c0de0001"
#define GTAW_TX_UUID      "6f9a0003-5a7c-4d2b-9e1f-47a5c0de0001"

enum : uint8_t {
  PKT_FIRST = 0x01,
  PKT_LAST = 0x02,
};

// phone -> watch
enum : uint8_t {
  MSG_TIME = 0x01,      // JSON {"t":unix_utc,"tz":offset_seconds}
  MSG_WEATHER = 0x02,   // JSON {"city","t","hi","lo","code","feels","hum","wind","hours":[[h,t,code]..]}
  MSG_GPS = 0x03,       // JSON {"lat","lon","hdg","spd","acc","street","area"}
  MSG_MAP = 0x04,       // binary road/water/park geometry, see mapdata.h
  MSG_NAV = 0x05,       // JSON turn-by-turn state, see state.h NavState
  MSG_ROUTE = 0x06,     // binary: i32 lat0e7, i32 lon0e7, u16 n, n*(i16 x, i16 y) in 0.5 m units
  MSG_NOTIFY = 0x07,    // JSON {"id","app","title","body"}
  MSG_PLACES = 0x08,    // JSON [{"n":name,"k":kind,"lat","lon"}]
  MSG_SETTINGS = 0x09,  // JSON {"h24","metric","timeout","bright","zoom"(0-2),"terrain","raise","face"(0-1),"keepOnNav"}
  MSG_PHONE = 0x0A,     // JSON {"bat","chg"} phone battery for the minimap armor bar
  MSG_PING = 0x0B,      // empty; watch answers with telemetry
  MSG_NOTIFY_CLEAR = 0x0C,
  MSG_TERRAIN = 0x0D,    // binary hillshade grid, see terrain.h
  MSG_OTA_BEGIN = 0x10,  // JSON {"size":bytes,"ver":"x.y.z"} -> watch erases the spare app slot
  MSG_OTA_DATA = 0x11,   // binary [u32 offset][bytes...] (<= 8 KB), each block ACKed via OTA_STATUS
  MSG_OTA_END = 0x12,    // empty -> verify, switch boot slot, restart
};

// watch -> phone
enum : uint8_t {
  MSG_TELEMETRY = 0x81,  // JSON {"fw","bat","mv","chg","steps","up","heap","psram","scr","page"}
  MSG_EVENT = 0x82,      // JSON {"e":name,...}: open_map, navigate{i}, save_parking, find_phone, nav_stop, hello,
                         //   battery_low{pct}, charged
  MSG_LOG = 0x83,        // text
  MSG_OTA_STATUS = 0x84, // JSON {"st":"ready|ack|done|error","off":bytes_written,"err":"..."}
};
