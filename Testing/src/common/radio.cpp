#include "radio.h"
#include "config.h"

static SX1276 lora = new Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_DIO1);
static RadioParams cur{0, 0, 0, 0};
static volatile bool irqFlag = false;
static uint32_t nextTxAllowed = 0;

static void IRAM_ATTR onDio0() { irqFlag = true; }

namespace Radio {

bool begin() {
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  int st = lora.begin(LORA_FREQ_MHZ, CTRL_BW, CTRL_SF, CTRL_CR,
                      LORA_SYNC_WORD, CTRL_POWER, LORA_PREAMBLE);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("[radio] begin() error %d\n", st);
    return false;
  }
  lora.setCRC(true);
  lora.setCurrentLimit(140);
  lora.setDio0Action(onDio0, RISING);
  cur = {CTRL_SF, CTRL_BW, CTRL_CR, CTRL_POWER};
  startRx();
  return true;
}

bool apply(const RadioParams& p) {
  lora.standby();
  bool ok = true;
  if (p.bw != cur.bw)       ok &= lora.setBandwidth(p.bw) == RADIOLIB_ERR_NONE;
  if (p.sf != cur.sf)       ok &= lora.setSpreadingFactor(p.sf) == RADIOLIB_ERR_NONE;
  if (p.cr != cur.cr)       ok &= lora.setCodingRate(p.cr) == RADIOLIB_ERR_NONE;
  if (p.power != cur.power) ok &= lora.setOutputPower(p.power) == RADIOLIB_ERR_NONE;
  // RadioLib activa LowDataRateOptimize automáticamente (SF11/12 @125k).
  if (ok) cur = p;
  else Serial.printf("[radio] parámetros no válidos SF%u BW%.1f CR4/%u %ddBm\n",
                     p.sf, p.bw, p.cr, p.power);
  startRx();
  return ok;
}

const RadioParams& current() { return cur; }

void applyCtrl() { apply({CTRL_SF, CTRL_BW, CTRL_CR, CTRL_POWER}); }

uint32_t airtimeMs(size_t len) {
  return (uint32_t)((lora.getTimeOnAir(len) + 999) / 1000);
}

uint32_t dutyWaitMs() {
  int32_t d = (int32_t)(nextTxAllowed - millis());
  return d > 0 ? (uint32_t)d : 0;
}

bool send(const void* data, size_t len) {
  uint32_t w = dutyWaitMs();
  if (w) delay(w);
  uint32_t t0 = millis();
  irqFlag = false;
  int st = lora.transmit((uint8_t*)data, len);
  uint32_t toa = millis() - t0;
  // Off-time tras la transmisión: toa * (1/d - 1)
  nextTxAllowed = millis() + (uint32_t)(toa * (1.0f / DUTY_CYCLE - 1.0f));
  startRx();
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("[radio] transmit error %d\n", st);
    return false;
  }
  return true;
}

void startRx() {
  irqFlag = false;
  lora.startReceive();
}

bool poll(uint8_t* buf, size_t maxLen, RxInfo& info) {
  if (!irqFlag) return false;
  irqFlag = false;
  size_t len = lora.getPacketLength();
  if (len > maxLen) len = maxLen;
  int st = lora.readData(buf, len);
  info.len = len;
  info.rssi = lora.getRSSI();
  info.snr = lora.getSNR();
  info.freqErr = lora.getFrequencyError();
  lora.startReceive();
  return st == RADIOLIB_ERR_NONE;
}

bool receive(uint8_t* buf, size_t maxLen, RxInfo& info, uint32_t timeoutMs) {
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    if (poll(buf, maxLen, info)) return true;
    delay(1);
  }
  return false;
}

}  // namespace Radio
