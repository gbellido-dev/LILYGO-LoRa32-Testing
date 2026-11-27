// ---------------------------------------------------------------------------
//  Nodo CASA: WiFi + MQTT. Recibe órdenes desde Node-RED (o botón BOOT /
//  puerto serie), ejecuta barridos de SF contra el nodo del garaje y publica
//  los resultados de cada ping y un resumen por SF.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "protocol.h"
#include "radio.h"
#include "display.h"

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #warning "Falta include/secrets.h: copia include/secrets.h.example"
  #include "secrets.h.example"
#endif

static WiFiClient   net;
static PubSubClient mqtt(net);
static String T(const char* sub) { return String(MQTT_PREFIX "/") + sub; }

// ---------------- Petición de barrido ----------------
struct Sweep {
  uint8_t  sfs[6] = {7, 8, 9, 10, 11, 12};
  uint8_t  nSf = 6;
  uint16_t count = 20;
  float    bw = 125.0;
  uint8_t  cr = 5;
  int8_t   power = 14;
  uint16_t interval = 0;   // 0 = automático según ciclo de trabajo
  bool     loop = false;   // repetir indefinidamente
};
static Sweep    pending;
static bool     startRequested = false;
static volatile bool stopRequested = false;
static bool     running = false;
static uint8_t  testIdCounter = 0;
static uint32_t sweepId = 0;
static char     statusLine[24] = "en espera";

// ---------------- Utilidades ----------------
static void publish(const char* sub, JsonDocument& doc, bool retain = false) {
  char out[400];
  size_t n = serializeJson(doc, out, sizeof out);
  Serial.printf("[%s] %s\n", sub, out);
  if (mqtt.connected()) mqtt.publish(T(sub).c_str(), (const uint8_t*)out, n, retain);
}

static void draw(const char* l3 = nullptr, const char* l4 = nullptr, const char* l5 = nullptr) {
  char l0[24], l1[24], l2[24];
  snprintf(l0, sizeof l0, "CASA %s %s", WiFi.isConnected() ? "WiFi" : "----",
           mqtt.connected() ? "MQTT" : "----");
  const RadioParams& p = Radio::current();
  snprintf(l1, sizeof l1, "SF%u BW%.0f CR4/%u %ddBm", p.sf, p.bw, p.cr, p.power);
  snprintf(l2, sizeof l2, "%s", statusLine);
  Display::show(l0, l1, l2, l3, l4, l5);
}

static void onMqtt(char* topic, byte* payload, unsigned int len) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, len)) { Serial.println("[cmd] JSON no válido"); return; }
  const char* action = doc["action"] | "start";
  if (!strcmp(action, "stop")) { stopRequested = true; return; }
  if (strcmp(action, "start")) return;
  Sweep s;
  if (doc["sf"].is<JsonArray>()) {
    s.nSf = 0;
    for (JsonVariant v : doc["sf"].as<JsonArray>()) {
      int sf = v.as<int>();
      if (sf >= 6 && sf <= 12 && s.nSf < 6) s.sfs[s.nSf++] = sf;
    }
    if (!s.nSf) return;
  } else if (doc["sf"].is<int>()) {
    s.sfs[0] = doc["sf"]; s.nSf = 1;
  }
  s.count    = constrain((int)(doc["count"] | 20), 1, 1000);
  s.bw       = doc["bw"] | 125.0;
  s.cr       = constrain((int)(doc["cr"] | 5), 5, 8);
  s.power    = constrain((int)(doc["power"] | 14), 2, 20);
  s.interval = doc["interval"] | 0;
  s.loop     = doc["loop"] | false;
  pending = s;
  startRequested = true;
  if (running) stopRequested = true;  // sustituye al barrido en curso
}

static void connectivity() {
  static uint32_t lastTry = 0;
  static int lastWifi = -1;
  int ws = WiFi.status();
  if (ws != lastWifi) {
    lastWifi = ws;
    if (ws == WL_CONNECTED)
      Serial.printf("[wifi] conectado a %s, IP %s, RSSI %d dBm\n", WIFI_SSID,
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
    else
      Serial.printf("[wifi] estado %d (%s)\n", ws,
                    ws == WL_NO_SSID_AVAIL ? "no encuentra la red" :
                    ws == WL_CONNECT_FAILED ? "fallo de conexion / clave" :
                    ws == WL_DISCONNECTED ? "desconectado" : "conectando");
  }
  if (ws != WL_CONNECTED) return;  // WiFi reconecta sola
  if (mqtt.connected()) { mqtt.loop(); return; }
  if (millis() - lastTry < 5000) return;
  lastTry = millis();
  String id = "lora32-home-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok = strlen(MQTT_USER)
      ? mqtt.connect(id.c_str(), MQTT_USER, MQTT_PASS, T("status").c_str(), 1, true, "offline")
      : mqtt.connect(id.c_str(), T("status").c_str(), 1, true, "offline");
  if (ok) {
    mqtt.publish(T("status").c_str(), "online", true);
    mqtt.subscribe(T("cmd").c_str());
    Serial.println("[mqtt] conectado");
  } else {
    Serial.printf("[mqtt] no conecta a %s:%d (estado %d)\n", MQTT_HOST, MQTT_PORT, mqtt.state());
  }
}

static void checkLocalInputs() {
  static uint32_t lastBtn = 0;
  if (digitalRead(BUTTON_PIN) == LOW && millis() - lastBtn > 1000) {
    lastBtn = millis();
    if (running) stopRequested = true;
    else { pending = Sweep(); startRequested = true; }
  }
  if (Serial.available()) {
    String l = Serial.readStringUntil('\n');
    l.trim();
    if (l == "stop") stopRequested = true;
    else if (l == "start") { pending = Sweep(); startRequested = true; }
    else if (l.startsWith("{")) onMqtt(nullptr, (byte*)l.c_str(), l.length());
  }
}

// Espera atendiendo MQTT / botón. Devuelve false si se pidió parar.
static bool serviceWait(uint32_t ms) {
  uint32_t t0 = millis();
  do {
    connectivity();
    checkLocalInputs();
    if (stopRequested) return false;
    delay(2);
  } while (millis() - t0 < ms);
  return true;
}

// Estimación de cuándo el garaje podrá volver a transmitir (su ciclo de
// trabajo). Si le hablamos antes, su respuesta saldría tarde y la perderíamos.
static uint32_t garageFreeAt = 0;

static void noteGarageTx(size_t len) {
  garageFreeAt = millis() + (uint32_t)(Radio::airtimeMs(len) * (1.0f / DUTY_CYCLE - 1.0f));
}

static bool sendWhenAllowed(const void* d, size_t len) {
  uint32_t w = Radio::dutyWaitMs();
  int32_t g = (int32_t)(garageFreeAt - (millis() + Radio::airtimeMs(len) + 100));
  if (g > (int32_t)w) w = g;
  if (!serviceWait(w)) return false;
  return Radio::send(d, len);
}

// Recibe un paquete del tipo y testId esperados dentro del timeout.
static bool waitFor(uint8_t type, uint8_t tid, uint8_t* buf, size_t max,
                    RxInfo& rx, uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (Radio::poll(buf, max, rx) && rx.len >= sizeof(PktHdr) &&
        buf[0] == PKT_MAGIC && buf[1] == type && buf[2] == tid) {
      noteGarageTx(rx.len);
      return true;
    }
    connectivity();
    if (stopRequested) return false;
    delay(1);
  }
  return false;
}

// ---------------- Una prueba (un SF) ----------------
static void runTest(const Sweep& s, uint8_t sf) {
  uint8_t tid = ++testIdCounter;
  RadioParams p{sf, s.bw, s.cr, s.power};

  // Calcula tiempos en el aire con los parámetros a probar
  Radio::apply(p);
  uint32_t toaPing = Radio::airtimeMs(sizeof(PktPing));
  uint32_t toaPong = Radio::airtimeMs(sizeof(PktPong));
  Radio::applyCtrl();
  uint32_t toaCtrl = Radio::airtimeMs(sizeof(PktReport));

  uint32_t toaMax = max(toaPing, toaPong);
  uint32_t minInt = max((uint32_t)(toaMax / DUTY_CYCLE) + 200, toaPing + toaPong + 300);
  uint32_t interval = max((uint32_t)s.interval, minInt);
  if (interval > 65535) interval = 65535;
  uint32_t pongTimeout = toaPong + 400;

  snprintf(statusLine, sizeof statusLine, "SF%u: configurando", sf);
  draw();

  // 1) CFG en canal de control (3 intentos)
  PktCfg cfg{{PKT_MAGIC, PKT_CFG, tid}, sf, (uint16_t)(s.bw * 10), s.cr, s.power,
             s.count, (uint16_t)interval};
  uint8_t buf[64];
  RxInfo rx;
  bool cfgOk = false;
  for (int i = 0; i < 3 && !cfgOk; i++) {
    if (!sendWhenAllowed(&cfg, sizeof cfg)) return;
    cfgOk = waitFor(PKT_CFG_ACK, tid, buf, sizeof buf, rx, toaCtrl + 600);
  }
  float ctrlRssi = cfgOk ? rx.rssi : NAN;

  // 2) Pings con los parámetros a probar
  Radio::apply(p);
  delay(100);  // el garaje también está cambiando
  uint16_t pongs = 0, lastGarageRx = 0;
  float rssiDn = 0, snrDn = 0, rssiUp = 0, snrUp = 0, rttSum = 0;
  float rssiDnMin = 0, snrDnMin = 0;
  bool lastPongOk = false;
  uint32_t t0 = millis();
  for (uint16_t seq = 0; seq < s.count; seq++) {
    uint32_t due = t0 + seq * interval;
    int32_t w = (int32_t)(due - millis());
    if (w > 0 && !serviceWait(w)) break;
    PktPing ping{{PKT_MAGIC, PKT_PING, tid}, seq};
    if (!sendWhenAllowed(&ping, sizeof ping)) break;
    uint32_t tx = millis();
    bool ok = waitFor(PKT_PONG, tid, buf, sizeof buf, rx, pongTimeout) &&
              ((PktPong*)buf)->seq == seq;
    uint32_t rtt = millis() - tx;
    lastPongOk = ok;

    JsonDocument d;
    d["sweep"] = sweepId; d["test"] = tid; d["sf"] = sf; d["seq"] = seq; d["ok"] = ok;
    if (ok) {
      PktPong* pg = (PktPong*)buf;
      float ru = pg->rssi10 / 10.0f, su = pg->snr4 / 4.0f;
      if (!pongs || rx.rssi < rssiDnMin) rssiDnMin = rx.rssi;
      if (!pongs || rx.snr < snrDnMin) snrDnMin = rx.snr;
      pongs++;
      lastGarageRx = pg->rxCount;
      rssiDn += rx.rssi; snrDn += rx.snr; rssiUp += ru; snrUp += su; rttSum += rtt;
      d["rssi_up"] = ru;  d["snr_up"] = su;      // casa -> garaje (medido en garaje)
      d["rssi_dn"] = rx.rssi; d["snr_dn"] = rx.snr; // garaje -> casa (medido en casa)
      d["ferr_hz"] = (int)rx.freqErr;
      d["rtt_ms"] = rtt;
    }
    publish("ping", d);

    char l3[24], l4[24], l5[24];
    snprintf(statusLine, sizeof statusLine, "SF%u: %u/%u ok %u", sf, seq + 1, s.count, pongs);
    snprintf(l3, sizeof l3, ok ? "dn %.0fdBm %.1fdB" : "dn  -- sin pong --", rx.rssi, rx.snr);
    snprintf(l4, sizeof l4, ok ? "up %.0fdBm %.1fdB" : " ", ((PktPong*)buf)->rssi10 / 10.0,
             ((PktPong*)buf)->snr4 / 4.0);
    snprintf(l5, sizeof l5, "int %lums ToA %lums", (unsigned long)interval,
             (unsigned long)toaPing);
    draw(l3, l4, l5);
  }
  bool aborted = stopRequested;

  // 3) De vuelta a control y pedir el informe del garaje
  Radio::applyCtrl();
  if (!lastPongOk && !aborted) {
    // el garaje puede seguir en modo prueba hasta que venza su ventana
    serviceWait(interval + 3200);
  }
  bool repOk = false;
  PktReport rep{};
  if (!aborted) {
    PktRepReq rq{{PKT_MAGIC, PKT_REP_REQ, tid}};
    for (int i = 0; i < 3 && !repOk; i++) {
      if (!sendWhenAllowed(&rq, sizeof rq)) break;
      repOk = waitFor(PKT_REPORT, tid, buf, sizeof buf, rx, toaCtrl + 600);
    }
    if (repOk) memcpy(&rep, buf, sizeof rep);
  }

  // 4) Resumen
  uint16_t sent = s.count;
  uint16_t gRx = repOk ? rep.rxCount : lastGarageRx;
  JsonDocument d;
  d["sweep"] = sweepId; d["test"] = tid; d["sf"] = sf; d["bw"] = s.bw; d["cr"] = s.cr;
  d["power"] = s.power; d["freq"] = LORA_FREQ_MHZ;
  d["toa_ms"] = toaPing; d["interval_ms"] = interval;
  d["cfg_ok"] = cfgOk; d["report_ok"] = repOk; d["aborted"] = aborted;
  if (cfgOk) d["ctrl_rssi"] = ctrlRssi;
  d["sent"] = sent; d["garage_rx"] = gRx; d["pongs"] = pongs;
  d["pdr_up"] = sent ? (float)gRx / sent : 0;                 // casa -> garaje
  d["pdr_dn"] = gRx ? (float)pongs / gRx : 0;                 // garaje -> casa
  d["pdr_rt"] = sent ? (float)pongs / sent : 0;               // ida y vuelta
  if (pongs) {
    d["rssi_dn"] = rssiDn / pongs; d["snr_dn"] = snrDn / pongs;
    d["rssi_dn_min"] = rssiDnMin; d["snr_dn_min"] = snrDnMin;
    d["rssi_up"] = rssiUp / pongs; d["snr_up"] = snrUp / pongs;
    d["rtt_ms"] = rttSum / pongs;
  }
  if (repOk && rep.rxCount) {
    d["rssi_up_all"] = rep.rssiAvg10 / 10.0;  // media de TODOS los pings oídos
    d["snr_up_all"] = rep.snrAvg10 / 10.0;
  }
  publish("summary", d);
}

static void runSweep(const Sweep& s) {
  running = true;
  stopRequested = false;
  do {
    sweepId = millis();
    {
      JsonDocument d;
      d["event"] = "start"; d["sweep"] = sweepId; d["count"] = s.count;
      d["bw"] = s.bw; d["cr"] = s.cr; d["power"] = s.power;
      JsonArray a = d["sf"].to<JsonArray>();
      for (int i = 0; i < s.nSf; i++) a.add(s.sfs[i]);
      publish("sweep", d);
    }
    for (int i = 0; i < s.nSf && !stopRequested; i++) runTest(s, s.sfs[i]);
    JsonDocument d;
    d["event"] = stopRequested ? "stopped" : "end"; d["sweep"] = sweepId;
    publish("sweep", d);
  } while (s.loop && !stopRequested);
  Radio::applyCtrl();
  stopRequested = false;
  running = false;
  snprintf(statusLine, sizeof statusLine, "en espera");
  draw();
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  Display::begin();
  Display::show("CASA", "iniciando radio...");
  if (!Radio::begin()) {
    Display::show("CASA", "ERROR radio LoRa", "revisa placa/pines");
    while (true) delay(1000);
  }
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(1024);
  mqtt.setCallback(onMqtt);
  Serial.println("[home] listo. 'start' / 'stop' / JSON por serie, o botón BOOT");
  draw("BOOT = barrido SF7-12", "o orden desde Node-RED");
}

void loop() {
  connectivity();
  checkLocalInputs();
  if (startRequested) {
    startRequested = false;
    Sweep s = pending;
    runSweep(s);
  }
  static uint32_t lastDraw = 0;
  if (millis() - lastDraw > 2000) {
    lastDraw = millis();
    draw("BOOT = barrido SF7-12", "o orden desde Node-RED");
  }
  delay(5);
}
