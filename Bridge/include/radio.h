#pragma once
#include <Arduino.h>
#include <RadioLib.h>

struct RadioParams {
  uint8_t sf;
  float   bw;      // kHz
  uint8_t cr;      // 5..8
  int8_t  power;   // dBm
};

struct RxInfo {
  size_t len;
  float  rssi;
  float  snr;
  float  freqErr;  // Hz
};

namespace Radio {
  bool begin();
  bool apply(const RadioParams& p);
  const RadioParams& current();
  void applyLink();

  // Envía respetando el ciclo de trabajo (espera si hace falta). Vuelve a RX.
  bool send(const void* data, size_t len);
  // Ms que faltan para poder transmitir según el ciclo de trabajo.
  uint32_t dutyWaitMs();

  void startRx();
  // Devuelve true si llegó un paquete válido (CRC OK) dentro del timeout.
  bool receive(uint8_t* buf, size_t maxLen, RxInfo& info, uint32_t timeoutMs);
  // Comprueba sin bloquear.
  bool poll(uint8_t* buf, size_t maxLen, RxInfo& info);

  // RSSI instantáneo del canal (ruido de fondo si nadie transmite), en dBm.
  // Debe llamarse en recepción.
  float channelRssi();

  // Tiempo en el aire con los parámetros actuales, en ms.
  uint32_t airtimeMs(size_t len);
}
