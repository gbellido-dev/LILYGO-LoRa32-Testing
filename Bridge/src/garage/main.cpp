// ---------------------------------------------------------------------------
//  Nodo GARAJE (Bridge): solo alimentación.
//  Lee sensores (PIR, puerta, BME280) y envía su estado por LoRa al nodo de
//  casa, cifrado con AES-128-CCM, con confirmación (ACK) autenticada y
//  reintentos.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include "config.h"
#include "protocol.h"
#include "radio.h"
#include "display.h"
#include "secure.h"
#if BME280_ADDR
  #include <Wire.h>
  #include <Adafruit_BME280.h>
#endif

static float    lastRssi = 0, lastSnr = 0;

// ---------------- Sensores ----------------
#if BME280_ADDR
static Adafruit_BME280 bme;
#endif
static bool     hasEnv = false;
static float    temp = NAN, hum = NAN, press = NAN;
static bool     motionNow = false, motionLatched = false;
static uint16_t motionEvents = 0;
static bool     doorOpen = false;
static uint32_t doorChangeMs = 0;
static float    ackRssi = 0;

// Estado confirmado por casa y envío pendiente
static bool     ackedMotion = false, ackedDoor = false;
static float    ackedTemp = NAN;
static bool     pending = true;          // enviar al arrancar
static uint8_t  txSeq = 0, attempts = 0;
static uint32_t nextAttempt = 0, lastSentOk = 0;
static SensorData outData;                       // datos del envío en curso
static uint8_t    outFrame[SENSOR_FRAME_LEN];    // trama cifrada (se reenvía igual)
static size_t     outLen = 0;
static uint8_t    outNonce[SEC_NONCE_LEN];       // para validar el ACK
static uint16_t   badAcks = 0;

static void readEnv() {
#if BME280_ADDR
  if (!hasEnv) return;
  float t = bme.readTemperature(), h = bme.readHumidity(), p = bme.readPressure();
  if (isnan(t) || t < -40 || t > 85) { temp = hum = press = NAN; return; }
  temp = t; hum = h; press = p / 100.0f;
#endif
}

static void readInputs() {
  if (PIR_PIN >= 0) {
    bool m = digitalRead(PIR_PIN) == PIR_ACTIVE;
    if (m && !motionNow) { motionEvents++; motionLatched = true; }
    motionNow = m;
  }
  if (DOOR_PIN >= 0) {
    bool o = digitalRead(DOOR_PIN) == DOOR_OPEN_LEVEL;
    if (o != doorOpen && millis() - doorChangeMs > 50) {  // antirrebote
      doorOpen = o;
      doorChangeMs = millis();
    }
  }
}

static bool reportedMotion() { return motionNow || motionLatched; }

// ¿Hay algo nuevo que contar a casa?
static void checkChanges() {
  if (reportedMotion() != ackedMotion || doorOpen != ackedDoor) pending = true;
  if (hasEnv && !isnan(temp) && (isnan(ackedTemp) || fabsf(temp - ackedTemp) >= TEMP_DELTA))
    pending = true;
  if (!lastSentOk || millis() - lastSentOk > HEARTBEAT_S * 1000UL) pending = true;
}

static void buildPacket() {
  uint8_t f = 0;
  if (PIR_PIN >= 0)  f |= SF_HAS_PIR  | (reportedMotion() ? SF_MOTION : 0);
  if (DOOR_PIN >= 0) f |= SF_HAS_DOOR | (doorOpen ? SF_DOOR_OPEN : 0);
  if (hasEnv && !isnan(temp)) f |= SF_HAS_ENV;
  outData = SensorData{f,
                       (int16_t)(isnan(temp) ? 0 : lroundf(temp * 100)),
                       (uint16_t)(isnan(hum) ? 0 : lroundf(hum * 10)),
                       (uint16_t)(isnan(press) ? 0 : lroundf(press * 10)),
                       motionEvents, (int16_t)(ackRssi * 10),
                       (uint16_t)(millis() / 60000UL)};
  PktHdr h{PKT_MAGIC, PKT_SENSOR, txSeq};
  outLen = Secure::seal(&h, sizeof h, &outData, sizeof outData, outFrame, sizeof outFrame,
                        outNonce);
}

static void serviceSensorTx() {
  if (!pending) return;
  if ((int32_t)(millis() - nextAttempt) < 0) return;
  if (attempts >= 4) {
    // Casa no contesta: descansa 15 s y vuelve a probar con el estado de entonces.
    attempts = 0;
    nextAttempt = millis() + 15000;
    Serial.println("[garage] sin ACK de casa tras 4 intentos");
    return;
  }
  if (Radio::dutyWaitMs()) return;
  if (attempts == 0) { txSeq++; buildPacket(); }   // nuevo envío: foto del estado actual
  if (!outLen) { pending = false; return; }       // no debería pasar
  Radio::send(outFrame, outLen);
  attempts++;
  // Espera del ACK: su tiempo en el aire + margen + algo aleatorio para no
  // chocar siempre con lo mismo.
  nextAttempt = millis() + Radio::airtimeMs(ACK_FRAME_LEN) + 400 + random(0, 600);
}

static void onSensorAck(const uint8_t* buf, const RxInfo& rx) {
  uint8_t seq = buf[2];
  if (seq != txSeq || !attempts) return;
  if (!Secure::openAck(buf, rx.len, sizeof(PktHdr), outNonce)) {
    badAcks++;
    Serial.println("[garage] ACK con firma NO válida: ignorado");
    return;
  }
  ackRssi = rx.rssi;
  attempts = 0;
  pending = false;
  lastSentOk = millis();
  ackedMotion = outData.flags & SF_MOTION;
  ackedDoor = outData.flags & SF_DOOR_OPEN;
  ackedTemp = (outData.flags & SF_HAS_ENV) ? outData.temp100 / 100.0f : NAN;
  // El pulso de movimiento ya está contado; si el PIR sigue activo, motionNow
  // lo mantiene, y cuando baje se enviará "sin movimiento".
  motionLatched = false;
  Serial.printf("[garage] ACK %u (RSSI %.1f)\n", seq, rx.rssi);
}

// ---------------- Pantalla ----------------
static void draw() {
  char l0[24], l1[24], l2[24], l3[24], l4[24], l5[24];
  const RadioParams& p = Radio::current();
  if (Secure::usingExampleKey()) snprintf(l0, sizeof l0, "GARAJE CLAVE EJEMPLO!");
  else if (badAcks) snprintf(l0, sizeof l0, "GARAJE  ACK malos %u", badAcks);
  else snprintf(l0, sizeof l0, "GARAJE  cifrado");
  snprintf(l1, sizeof l1, "SF%u BW%.0f CR4/%u %ddBm", p.sf, p.bw, p.cr, p.power);
  snprintf(l2, sizeof l2, "Mov %s  Puerta %s",
           PIR_PIN < 0 ? "--" : motionNow ? "SI" : "no",
           DOOR_PIN < 0 ? "--" : doorOpen ? "ABIER" : "cerr");
  if (hasEnv && !isnan(temp)) snprintf(l3, sizeof l3, "%.1fC %.0f%% %.0fhPa", temp, hum, press);
  else snprintf(l3, sizeof l3, "sin BME280");
  snprintf(l4, sizeof l4, "casa: %.0fdBm %.1fdB", lastRssi, lastSnr);
  if (pending && attempts) snprintf(l5, sizeof l5, "enviando... (%u)", attempts);
  else if (lastSentOk) snprintf(l5, sizeof l5, "ult. ACK hace %lus", (millis() - lastSentOk) / 1000);
  else snprintf(l5, sizeof l5, "sin contacto con casa");
  Display::show(l0, l1, l2, l3, l4, l5);
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  if (PIR_PIN >= 0) pinMode(PIR_PIN, INPUT);
  if (DOOR_PIN >= 0) pinMode(DOOR_PIN, INPUT_PULLUP);
  Display::begin();  // también inicializa el bus I2C
  Display::show("GARAJE", "iniciando radio...");
#if BME280_ADDR
  hasEnv = bme.begin(BME280_ADDR, &Wire);
  Serial.printf("[garage] BME280 %s\n", hasEnv ? "OK" : "no encontrado");
#endif
  if (!Radio::begin()) {
    Display::show("GARAJE", "ERROR radio LoRa", "revisa placa/pines");
    while (true) delay(1000);
  }
  if (!Secure::begin()) {
    Display::show("GARAJE", "ERROR cifrado", "revisa LORA_KEY");
    while (true) delay(1000);
  }
  if (Secure::usingExampleKey())
    Serial.println("[garage] AVISO: clave de ejemplo. Cambia LORA_KEY en secrets.h");
  randomSeed(esp_random());
  if (DOOR_PIN >= 0) doorOpen = digitalRead(DOOR_PIN) == DOOR_OPEN_LEVEL;
  readEnv();
  Serial.println("[garage] listo");
  draw();
}

void loop() {
  static uint32_t lastDraw = 0, lastEnv = 0;
  uint8_t buf[64];
  RxInfo rx;
  if (Radio::poll(buf, sizeof buf, rx) && rx.len >= sizeof(PktHdr) && buf[0] == PKT_MAGIC &&
      buf[1] == PKT_SENS_ACK) {
    lastRssi = rx.rssi;
    lastSnr = rx.snr;
    digitalWrite(LED_PIN, HIGH);
    onSensorAck(buf, rx);
    digitalWrite(LED_PIN, LOW);
  }

  readInputs();
  if (millis() - lastEnv > 10000) { lastEnv = millis(); readEnv(); }
  checkChanges();
  serviceSensorTx();

  if (millis() - lastDraw > 1000) { lastDraw = millis(); draw(); }
  delay(1);
}
