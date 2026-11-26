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
  PKT_SENSOR  = 10,  // garaje -> casa (canal de enlace). h.testId = nº secuencia
  PKT_SENS_ACK= 11,   // casa -> garaje. h.testId = nº secuencia confirmado
};

// Bits de PktSensor.flags
#define SF_MOTION    0x01   // hay (o ha habido desde el último envío) movimiento
#define SF_DOOR_OPEN 0x02
#define SF_HAS_PIR   0x10
#define SF_HAS_DOOR  0x20
#define SF_HAS_ENV   0x40   // BME280 presente y lectura válida

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

struct __attribute__((packed)) PktSensor {
  PktHdr   h;
  uint8_t  flags;
  int16_t  temp100;       // °C * 100
  uint16_t hum10;         // %HR * 10
  uint16_t press10;       // hPa * 10
  uint16_t motionEvents;  // contador de activaciones del PIR desde el arranque
  int16_t  ackRssi10;     // RSSI del último ACK oído en el garaje (enlace casa->garaje)
  uint16_t uptimeMin;
};

struct __attribute__((packed)) PktSensAck {
  PktHdr h;
};
