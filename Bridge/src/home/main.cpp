// ---------------------------------------------------------------------------
//  Nodo CASA (Bridge): WiFi + MQTT.
//  Recibe por LoRa el estado de los sensores del garaje, lo confirma (ACK) y
//  lo publica en MQTT con descubrimiento automático para Home Assistant.
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

#ifndef HA_PREFIX
#define HA_PREFIX "homeassistant"   // prefijo de MQTT Discovery de Home Assistant
#endif

static WiFiClient   net;
static PubSubClient mqtt(net);
static String T(const char* sub) { return String(MQTT_PREFIX "/") + sub; }

static void publish(const char* sub, JsonDocument& doc, bool retain = false) {
  char out[400];
  size_t n = serializeJson(doc, out, sizeof out);
  Serial.printf("[%s] %s\n", sub, out);
  if (mqtt.connected()) mqtt.publish(T(sub).c_str(), (const uint8_t*)out, n, retain);
}

// ---------------- Puente garaje -> Home Assistant ----------------
struct Garage {
  bool     valid = false;
  bool     online = false;
  uint8_t  flags = 0;
  uint8_t  announced = 0;   // capacidades ya anunciadas a HA (SF_HAS_*)
  float    temp = NAN, hum = NAN, press = NAN;
  uint16_t events = 0, uptime = 0;
  float    rssi = 0, snr = 0, rssiUp = 0;
  uint32_t lastRx = 0;
};
static Garage g;

static void pubRaw(const String& topic, const char* payload, bool retain) {
  if (mqtt.connected()) mqtt.publish(topic.c_str(), payload, retain);
}

static void haEntity(const char* comp, const char* obj, const char* name,
                     const char* devClass, const char* unit, const char* tpl,
                     bool diagnostic = false, const char* stateClass = "measurement") {
  JsonDocument d;
  d["name"] = name;
  d["unique_id"] = String("lora32_garage_") + obj;
  d["object_id"] = String("garaje_") + obj;
  d["state_topic"] = T("garage/state");
  d["value_template"] = tpl;
  if (devClass) d["device_class"] = devClass;
  if (!strcmp(comp, "sensor")) {
    if (unit) d["unit_of_measurement"] = unit;
    if (stateClass) d["state_class"] = stateClass;
  }
  if (diagnostic) d["entity_category"] = "diagnostic";
  // Disponible solo si el nodo de casa está conectado Y oye al garaje
  JsonArray av = d["availability"].to<JsonArray>();
  av.add<JsonObject>()["topic"] = T("status");
  av.add<JsonObject>()["topic"] = T("garage/availability");
  d["availability_mode"] = "all";
  JsonObject dev = d["device"].to<JsonObject>();
  dev["identifiers"].to<JsonArray>().add("lora32_garage");
  dev["name"] = "Garaje (LoRa)";
  dev["manufacturer"] = "LILYGO";
  dev["model"] = "LoRa32 SX1276";
  dev["via_device"] = "lora32_home";
  char out[1024];
  serializeJson(d, out, sizeof out);
  pubRaw(String(HA_PREFIX "/") + comp + "/lora32_garage/" + obj + "/config", out, true);
}

static void haAnnounce() {
  if (!mqtt.connected() || !g.valid) return;
  // Dispositivo "puente" (el propio nodo de casa) para via_device
  {
    JsonDocument d;
    d["name"] = "Enlace LoRa";
    d["unique_id"] = "lora32_home_status";
    d["state_topic"] = T("status");
    d["payload_on"] = "online";
    d["payload_off"] = "offline";
    d["device_class"] = "connectivity";
    d["entity_category"] = "diagnostic";
    JsonObject dev = d["device"].to<JsonObject>();
    dev["identifiers"].to<JsonArray>().add("lora32_home");
    dev["name"] = "Puente LoRa (casa)";
    dev["manufacturer"] = "LILYGO";
    dev["model"] = "LoRa32 SX1276";
    char out[600];
    serializeJson(d, out, sizeof out);
    pubRaw(HA_PREFIX "/binary_sensor/lora32_home/status/config", out, true);
  }
  if (g.flags & SF_HAS_PIR)
    haEntity("binary_sensor", "motion", "Movimiento", "motion", nullptr,
             "{{ 'ON' if value_json.motion else 'OFF' }}");
  if (g.flags & SF_HAS_DOOR)
    haEntity("binary_sensor", "door", "Puerta", "door", nullptr,
             "{{ 'ON' if value_json.door_open else 'OFF' }}");
  if (g.flags & SF_HAS_ENV) {
    haEntity("sensor", "temperature", "Temperatura", "temperature", "°C",
             "{{ value_json.temperature }}");
    haEntity("sensor", "humidity", "Humedad", "humidity", "%",
             "{{ value_json.humidity }}");
    haEntity("sensor", "pressure", "Presión", "atmospheric_pressure", "hPa",
             "{{ value_json.pressure }}");
  }
  if (g.flags & SF_HAS_PIR)
    haEntity("sensor", "motion_events", "Activaciones PIR", nullptr, nullptr,
             "{{ value_json.motion_events }}", true, "total_increasing");
  haEntity("sensor", "rssi", "RSSI garaje→casa", "signal_strength", "dBm",
           "{{ value_json.rssi }}", true);
  haEntity("sensor", "snr", "SNR garaje→casa", "signal_strength", "dB",
           "{{ value_json.snr }}", true);
  haEntity("sensor", "rssi_up", "RSSI casa→garaje", "signal_strength", "dBm",
           "{{ value_json.rssi_up }}", true);
  haEntity("sensor", "uptime", "Tiempo encendido", "duration", "min",
           "{{ value_json.uptime_min }}", true, "total_increasing");
  g.announced = g.flags & (SF_HAS_PIR | SF_HAS_DOOR | SF_HAS_ENV);
}

static void setGarageOnline(bool on) {
  g.online = on;
  pubRaw(T("garage/availability"), on ? "online" : "offline", true);
}

static void publishGarage() {
  JsonDocument d;
  if (g.flags & SF_HAS_PIR) d["motion"] = (bool)(g.flags & SF_MOTION);
  if (g.flags & SF_HAS_DOOR) d["door_open"] = (bool)(g.flags & SF_DOOR_OPEN);
  if (g.flags & SF_HAS_ENV) {
    d["temperature"] = roundf(g.temp * 10) / 10;
    d["humidity"] = roundf(g.hum);
    d["pressure"] = roundf(g.press * 10) / 10;
  }
  d["motion_events"] = g.events;
  d["rssi"] = roundf(g.rssi);
  d["snr"] = roundf(g.snr * 10) / 10;
  if (g.rssiUp) d["rssi_up"] = roundf(g.rssiUp);
  d["uptime_min"] = g.uptime;
  publish("garage/state", d, true);
}

// Paquete de sensores del garaje: confirmar y publicar.
static void handleSensor(const uint8_t* buf, const RxInfo& rx, bool ack) {
  if (rx.len < sizeof(PktSensor)) return;
  PktSensor p;
  memcpy(&p, buf, sizeof p);
  // Si el ciclo de trabajo no nos deja contestar ya, no bloqueamos: se publica
  // igual y el garaje lo reenviará.
  if (ack && Radio::dutyWaitMs() < 100) {
    PktSensAck a{{PKT_MAGIC, PKT_SENS_ACK, p.h.seq}};
    delay(30);
    Radio::send(&a, sizeof a);
  }
  bool capsChanged = (p.flags & (SF_HAS_PIR | SF_HAS_DOOR | SF_HAS_ENV)) != g.announced;
  g.valid = true;
  g.flags = p.flags;
  g.temp = p.temp100 / 100.0f;
  g.hum = p.hum10 / 10.0f;
  g.press = p.press10 / 10.0f;
  g.events = p.motionEvents;
  g.uptime = p.uptimeMin;
  g.rssiUp = p.ackRssi10 / 10.0f;
  g.rssi = rx.rssi;
  g.snr = rx.snr;
  g.lastRx = millis();
  if (capsChanged) haAnnounce();
  if (!g.online) setGarageOnline(true);
  publishGarage();
}

static void checkGarageTimeout() {
  if (g.online && millis() - g.lastRx > HEARTBEAT_S * 3000UL + 30000) setGarageOnline(false);
}

static void connectivity() {
  static uint32_t lastTry = 0;
  checkGarageTimeout();
  if (WiFi.status() != WL_CONNECTED) return;  // WiFi reconecta sola
  if (mqtt.connected()) { mqtt.loop(); return; }
  if (millis() - lastTry < 5000) return;
  lastTry = millis();
  String id = "lora32-bridge-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  bool ok = strlen(MQTT_USER)
      ? mqtt.connect(id.c_str(), MQTT_USER, MQTT_PASS, T("status").c_str(), 1, true, "offline")
      : mqtt.connect(id.c_str(), T("status").c_str(), 1, true, "offline");
  if (ok) {
    mqtt.publish(T("status").c_str(), "online", true);
    Serial.println("[mqtt] conectado");
    if (g.valid) { haAnnounce(); setGarageOnline(g.online); publishGarage(); }
  }
}

static void drawStatus() {
  char l0[24], l1[24], l2[24], l3[24], l4[24], l5[24];
  snprintf(l0, sizeof l0, "CASA %s %s", WiFi.isConnected() ? "WiFi" : "----",
           mqtt.connected() ? "MQTT" : "----");
  const RadioParams& p = Radio::current();
  snprintf(l1, sizeof l1, "SF%u BW%.0f CR4/%u %ddBm", p.sf, p.bw, p.cr, p.power);
  if (!g.valid) {
    Display::show(l0, l1, "Garaje: sin datos");
    return;
  }
  snprintf(l2, sizeof l2, "Mov %s Puerta %s",
           !(g.flags & SF_HAS_PIR) ? "--" : (g.flags & SF_MOTION) ? "SI" : "no",
           !(g.flags & SF_HAS_DOOR) ? "--" : (g.flags & SF_DOOR_OPEN) ? "ABIER" : "cerr");
  if (g.flags & SF_HAS_ENV) snprintf(l3, sizeof l3, "%.1fC %.0f%% %.0fhPa", g.temp, g.hum, g.press);
  else snprintf(l3, sizeof l3, " ");
  snprintf(l4, sizeof l4, "%.0fdBm %.1fdB", g.rssi, g.snr);
  snprintf(l5, sizeof l5, "%s hace %lus", g.online ? "garaje" : "PERDIDO",
           (unsigned long)((millis() - g.lastRx) / 1000));
  Display::show(l0, l1, l2, l3, l4, l5);
}

void setup() {
  Serial.begin(115200);
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
  Serial.println("[home] bridge listo");
  drawStatus();
}

void loop() {
  connectivity();
  uint8_t buf[64];
  RxInfo rx;
  if (Radio::poll(buf, sizeof buf, rx) && rx.len >= sizeof(PktHdr) && buf[0] == PKT_MAGIC &&
      buf[1] == PKT_SENSOR) {
    digitalWrite(LED_PIN, HIGH);
    handleSensor(buf, rx, true);
    digitalWrite(LED_PIN, LOW);
    drawStatus();
  }
  static uint32_t lastDraw = 0;
  if (millis() - lastDraw > 2000) {
    lastDraw = millis();
    drawStatus();
  }
  delay(5);
}
