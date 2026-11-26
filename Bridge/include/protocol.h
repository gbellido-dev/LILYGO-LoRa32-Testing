#pragma once
#include <stdint.h>

// ---------------------------------------------------------------------------
//  Protocolo garaje -> casa.
//   SENSOR    garaje -> casa  estado completo de los sensores
//   SENS_ACK  casa -> garaje  confirmación (mismo nº de secuencia)
//  Siempre con los parámetros del enlace (LINK_* en config.h).
// ---------------------------------------------------------------------------

#define PKT_MAGIC 0xB7   // distinto del proyecto Testing para no mezclarse

enum PktType : uint8_t {
  PKT_SENSOR   = 10,
  PKT_SENS_ACK = 11,
};

struct __attribute__((packed)) PktHdr {
  uint8_t magic;
  uint8_t type;
  uint8_t seq;
};

// Bits de PktSensor.flags
#define SF_MOTION    0x01   // hay (o ha habido desde el último envío) movimiento
#define SF_DOOR_OPEN 0x02
#define SF_HAS_PIR   0x10
#define SF_HAS_DOOR  0x20
#define SF_HAS_ENV   0x40   // BME280 presente y lectura válida

struct __attribute__((packed)) PktSensor {
  PktHdr   h;
  uint8_t  flags;
  int16_t  temp100;       // °C * 100
  uint16_t hum10;         // %HR * 10
  uint16_t press10;       // hPa * 10
  uint16_t motionEvents;  // activaciones del PIR desde el arranque
  int16_t  ackRssi10;     // RSSI del último ACK oído en el garaje (casa->garaje)
  uint16_t uptimeMin;
};

struct __attribute__((packed)) PktSensAck {
  PktHdr h;
};
