#include "phone.h"

#include <NimBLEDevice.h>

#include "hal.h"
#include "proto.h"
#include "state.h"
#include "ui.h"

struct InMsg {
  uint8_t type;
  size_t len;
  uint8_t data[1];  // variable length, allocated in PSRAM
};

static QueueHandle_t inbox;
static NimBLECharacteristic *txChar = nullptr;
static NimBLEServer *server = nullptr;
static volatile bool isConnected = false;
static volatile bool subscribed = false;
static volatile uint16_t connHandle = BLE_HS_CONN_HANDLE_NONE;
static volatile int negotiatedMtu = 23;
static volatile uint32_t connectedAtMs = 0;

// Reassembly buffer for chunked RX messages (written only from the NimBLE host task).
static uint8_t *rxBuf = nullptr;
static size_t rxLen = 0;
static uint8_t rxType = 0;
static bool rxActive = false;
static constexpr size_t RX_MAX = 256 * 1024;

namespace phone {

void enqueue(uint8_t type, const uint8_t *data, size_t len) {
  InMsg *m = (InMsg *)heap_caps_malloc(sizeof(InMsg) + len, MALLOC_CAP_SPIRAM);
  if (!m) return;
  m->type = type;
  m->len = len;
  if (len) memcpy(m->data, data, len);
  m->data[len] = 0;  // lets JSON payloads be used as C strings
  if (xQueueSend(inbox, &m, 0) != pdTRUE) free(m);
}

class ServerCB : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *s, NimBLEConnInfo &info) override {
    isConnected = true;
    connHandle = info.getConnHandle();
    connectedAtMs = millis();
    // 15-30 ms interval, no latency, 5 s supervision timeout: responsive but tolerant of pocket fades.
    s->updateConnParams(info.getConnHandle(), 12, 24, 0, 500);
  }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int reason) override {
    isConnected = false;
    subscribed = false;
    connHandle = BLE_HS_CONN_HANDLE_NONE;
    negotiatedMtu = 23;
    rxActive = false;
    Serial.printf("[ble] disconnected, reason 0x%x\n", reason);
    NimBLEDevice::startAdvertising();
  }
  void onMTUChange(uint16_t m, NimBLEConnInfo &) override { negotiatedMtu = m; }
};

class RxCB : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c, NimBLEConnInfo &) override {
    NimBLEAttValue v = c->getValue();
    const uint8_t *p = v.data();
    size_t n = v.size();
    if (n < 2) return;
    uint8_t type = p[0], flags = p[1];
    p += 2;
    n -= 2;
    if (flags & PKT_FIRST) {
      rxActive = true;
      rxType = type;
      rxLen = 0;
    }
    if (!rxActive || type != rxType) return;
    if (rxLen + n > RX_MAX) {
      rxActive = false;
      return;
    }
    memcpy(rxBuf + rxLen, p, n);
    rxLen += n;
    if (flags & PKT_LAST) {
      rxActive = false;
      enqueue(rxType, rxBuf, rxLen);
    }
  }
};

class TxCB : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &, uint16_t subValue) override { subscribed = subValue != 0; }
};

void begin() {
  inbox = xQueueCreate(24, sizeof(InMsg *));
  rxBuf = (uint8_t *)heap_caps_malloc(RX_MAX, MALLOC_CAP_SPIRAM);

  NimBLEDevice::init("GTA-Watch");
  NimBLEDevice::setPower(9);
  NimBLEDevice::setMTU(517);
  server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCB());
  server->advertiseOnDisconnect(true);

  NimBLEService *svc = server->createService(GTAW_SERVICE_UUID);
  NimBLECharacteristic *rx = svc->createCharacteristic(GTAW_RX_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rx->setCallbacks(new RxCB());
  txChar = svc->createCharacteristic(GTAW_TX_UUID, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  txChar->setCallbacks(new TxCB());
  svc->start();

  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(GTAW_SERVICE_UUID);
  adv->setName("GTA-Watch");
  adv->enableScanResponse(true);
  adv->setMinInterval(160);  // 100 ms: quick reconnects
  adv->setMaxInterval(320);
  adv->start();
}

bool connected() { return isConnected; }
int mtu() { return negotiatedMtu; }

void send(uint8_t type, const uint8_t *data, size_t len) {
  // Mirror everything to USB so the PC tools can see what the phone would get.
  if (type != MSG_LOG) Serial.printf("[tx] %02x %.*s\n", type, (int)min<size_t>(len, 200), (const char *)data);
  if (!isConnected || !subscribed || !txChar) return;
  const size_t chunk = max(20, negotiatedMtu - 3) - 2;
  uint8_t pkt[520];
  size_t off = 0;
  do {
    size_t n = min(chunk, len - off);
    pkt[0] = type;
    pkt[1] = (off == 0 ? PKT_FIRST : 0) | (off + n >= len ? PKT_LAST : 0);
    memcpy(pkt + 2, data + off, n);
    for (int tries = 0; tries < 20; tries++) {
      if (txChar->notify(pkt, n + 2, connHandle)) break;
      delay(5);
    }
    off += n;
  } while (off < len);
}

void sendJson(uint8_t type, const char *json) { send(type, (const uint8_t *)json, strlen(json)); }

void sendEvent(const char *name, int index) {
  char buf[96];
  if (index >= 0) snprintf(buf, sizeof(buf), "{\"e\":\"%s\",\"i\":%d}", name, index);
  else snprintf(buf, sizeof(buf), "{\"e\":\"%s\"}", name);
  sendJson(MSG_EVENT, buf);
}

void sendTelemetry() {
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"fw\":\"%s\",\"bat\":%d,\"mv\":%d,\"chg\":%s,\"usb\":%s,\"steps\":%lu,\"up\":%lu,\"heap\":%u,\"psram\":%u,\"scr\":%s,\"page\":\"%s\",\"mtu\":%d}",
           FW_VERSION, hal::batteryPercent(), hal::batteryMillivolts(), hal::charging() ? "true" : "false",
           hal::usbPowered() ? "true" : "false", (unsigned long)hal::stepsToday(), (unsigned long)(millis() / 1000),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           hal::screenIsOn() ? "true" : "false", ui::pageName(), negotiatedMtu);
  sendJson(MSG_TELEMETRY, buf);
}

void loop() {
  InMsg *m;
  int budget = 6;  // keep the UI responsive while a big map streams in
  while (budget-- > 0 && xQueueReceive(inbox, &m, 0) == pdTRUE) {
    state::handleMessage(m->type, m->data, m->len);
    if (m->type == MSG_PING) sendTelemetry();
    free(m);
  }

  bool c = isConnected;
  if (c != app.connected) {
    app.connected = c;
    ui::onConnectionChanged(c);
  }

  static bool greeted = false;
  static uint32_t lastTelemetry = 0;
  if (c && subscribed) {
    if (!greeted) {
      greeted = true;
      sendEvent("hello");
      sendTelemetry();
      lastTelemetry = millis();
    }
    if (millis() - lastTelemetry > 15000) {
      lastTelemetry = millis();
      sendTelemetry();
    }
  } else {
    greeted = false;
  }
}

}  // namespace phone
