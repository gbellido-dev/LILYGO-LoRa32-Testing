// ---------------------------------------------------------------------------
//  Nodo GARAJE: solo alimentación. Escucha en el canal de control (SF12),
//  acepta la configuración que pide el nodo de casa, responde a los pings
//  con el RSSI/SNR que ha medido y vuelve al canal de control.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include "config.h"
#include "protocol.h"
#include "radio.h"
#include "display.h"

enum State { ST_CTRL, ST_TEST };
static State state = ST_CTRL;

// Estadísticas de la última prueba (se envían en el REPORT)
static uint8_t  testId = 0;
static PktCfg   cfg;
static uint16_t rxCount = 0, txCount = 0;
static float    rssiSum = 0, snrSum = 0;
static uint32_t testDeadline = 0;
static float    lastRssi = 0, lastSnr = 0;
static uint32_t lastRxMs = 0;

static void draw() {
  char l0[24], l1[24], l2[24], l3[24], l4[24];
  const RadioParams& p = Radio::current();
  snprintf(l0, sizeof l0, "GARAJE  %s", state == ST_CTRL ? "esperando" : "PRUEBA");
  snprintf(l1, sizeof l1, "SF%u BW%.0f CR4/%u %ddBm", p.sf, p.bw, p.cr, p.power);
  snprintf(l2, sizeof l2, "test #%u  rx %u/%u", testId, rxCount, cfg.count);
  snprintf(l3, sizeof l3, "RSSI %.1f dBm", lastRssi);
  snprintf(l4, sizeof l4, "SNR  %.1f dB", lastSnr);
  char l5[24];
  if (lastRxMs) snprintf(l5, sizeof l5, "ult. rx hace %lus", (millis() - lastRxMs) / 1000);
  else          snprintf(l5, sizeof l5, "%.3f MHz", LORA_FREQ_MHZ);
  Display::show(l0, l1, l2, l3, l4, l5);
}

static void backToCtrl() {
  Radio::applyCtrl();
  state = ST_CTRL;
  Serial.printf("[garage] fin prueba #%u: rx=%u tx=%u -> canal control\n",
                testId, rxCount, txCount);
  draw();
}

static void handleCtrl(const uint8_t* buf, const RxInfo& rx) {
  const PktHdr* h = (const PktHdr*)buf;
  if (h->type == PKT_CFG && rx.len >= sizeof(PktCfg)) {
    memcpy(&cfg, buf, sizeof cfg);
    testId = cfg.h.testId;
    rxCount = txCount = 0;
    rssiSum = snrSum = 0;
    Serial.printf("[garage] CFG #%u: SF%u BW%.1f CR4/%u %ddBm n=%u int=%ums\n",
                  testId, cfg.sf, cfg.bwKhz10 / 10.0, cfg.cr, cfg.power,
                  cfg.count, cfg.intervalMs);
    PktCfgAck ack{{PKT_MAGIC, PKT_CFG_ACK, testId}};
    delay(50);  // deja que el maestro pase a RX
    Radio::send(&ack, sizeof ack);
    RadioParams p{cfg.sf, cfg.bwKhz10 / 10.0f, cfg.cr, cfg.power};
    if (!Radio::apply(p)) { Radio::applyCtrl(); return; }
    // Misma ventana que usa el maestro, más margen
    testDeadline = millis() + (uint32_t)cfg.count * cfg.intervalMs + 3000;
    state = ST_TEST;
    draw();
  } else if (h->type == PKT_REP_REQ && h->testId == testId) {
    PktReport r{{PKT_MAGIC, PKT_REPORT, testId}, rxCount, txCount,
                (int16_t)(rxCount ? rssiSum / rxCount * 10 : 0),
                (int16_t)(rxCount ? snrSum / rxCount * 10 : 0)};
    delay(50);
    Radio::send(&r, sizeof r);
    Serial.printf("[garage] REPORT #%u enviado\n", testId);
  }
}

static void handleTest(const uint8_t* buf, const RxInfo& rx) {
  const PktHdr* h = (const PktHdr*)buf;
  if (h->type != PKT_PING || h->testId != testId || rx.len < sizeof(PktPing)) return;
  const PktPing* ping = (const PktPing*)buf;
  rxCount++;
  // Re-sincroniza la ventana con el maestro a cada ping recibido
  testDeadline = millis() + (uint32_t)(cfg.count - ping->seq) * cfg.intervalMs + 3000;
  rssiSum += rx.rssi;
  snrSum += rx.snr;
  PktPong pong{{PKT_MAGIC, PKT_PONG, testId}, ping->seq,
               (int16_t)(rx.rssi * 10), (int8_t)(rx.snr * 4), rxCount};
  delay(20);
  if (Radio::send(&pong, sizeof pong)) txCount++;
  Serial.printf("[garage] ping %u RSSI %.1f SNR %.2f ferr %.0fHz\n",
                ping->seq, rx.rssi, rx.snr, rx.freqErr);
  draw();
  if (ping->seq + 1 >= cfg.count) backToCtrl();
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  Display::begin();
  Display::show("GARAJE", "iniciando radio...");
  if (!Radio::begin()) {
    Display::show("GARAJE", "ERROR radio LoRa", "revisa placa/pines");
    while (true) delay(1000);
  }
  Serial.println("[garage] listo, escuchando canal de control");
  draw();
}

void loop() {
  static uint32_t lastDraw = 0;
  uint8_t buf[64];
  RxInfo rx;
  if (Radio::poll(buf, sizeof buf, rx)) {
    if (rx.len >= sizeof(PktHdr) && buf[0] == PKT_MAGIC) {
      lastRssi = rx.rssi;
      lastSnr = rx.snr;
      lastRxMs = millis();
      digitalWrite(LED_PIN, HIGH);
      if (state == ST_CTRL) handleCtrl(buf, rx);
      else handleTest(buf, rx);
      digitalWrite(LED_PIN, LOW);
    }
  }
  if (state == ST_TEST && (int32_t)(millis() - testDeadline) > 0) backToCtrl();
  if (millis() - lastDraw > 1000) { lastDraw = millis(); draw(); }
  delay(1);
}
