#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
//  Protocolo entre "home" (maestro) y "garage" (respondedor).
//
//  1. CFG      home -> garage   (canal de control SF12)  "vamos a probar X"
//  2. CFG_ACK  garage -> home   (canal de control)
//  3. PING/PONG N veces         (con los parámetros a probar)
//  4. REP_REQ / REPORT          (canal de control) cuántos pings oyó el garaje
//
//  El garaje vuelve solo al canal de control al responder el último ping o
//  cuando vence la ventana de prueba, así nunca se queda "perdido".
// ---------------------------------------------------------------------------

#define PKT_MAGIC 0xA7

enum PktType : uint8_t {
  PKT_CFG     = 1,
  PKT_CFG_ACK = 2,
  PKT_PING    = 3,
  PKT_PONG    = 4,
  PKT_REP_REQ = 5,
  PKT_REPORT  = 6,
};

struct __attribute__((packed)) PktHdr {
  uint8_t magic;
  uint8_t type;
  uint8_t testId;
};

struct __attribute__((packed)) PktCfg {
  PktHdr  h;
  uint8_t sf;
  uint16_t bwKhz10;   // ancho de banda en kHz*10 (1250 = 125 kHz)
  uint8_t cr;         // 5..8 -> 4/5..4/8
  int8_t  power;      // dBm
  uint16_t count;     // nº de pings
  uint16_t intervalMs;// separación entre pings
};

struct __attribute__((packed)) PktCfgAck {
  PktHdr h;
};

struct __attribute__((packed)) PktPing {
  PktHdr   h;
  uint16_t seq;
};

struct __attribute__((packed)) PktPong {
  PktHdr   h;
  uint16_t seq;
  int16_t  rssi10;    // RSSI del ping visto en el garaje, dBm*10
  int8_t   snr4;      // SNR del ping visto en el garaje, dB*4
  uint16_t rxCount;   // pings recibidos hasta ahora en esta prueba
};

struct __attribute__((packed)) PktRepReq {
  PktHdr h;
};

struct __attribute__((packed)) PktReport {
  PktHdr   h;
  uint16_t rxCount;   // pings recibidos en total
  uint16_t txCount;   // pongs enviados
  int16_t  rssiAvg10;
  int16_t  snrAvg10;
};
