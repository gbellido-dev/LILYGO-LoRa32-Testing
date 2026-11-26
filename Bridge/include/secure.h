#pragma once
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
//  Cifrado + autenticación de los paquetes LoRa con AES-128-CCM (como
//  LoRaWAN), con la clave compartida LORA_KEY de secrets.h.
//
//  Trama de datos:  [cabecera 3 B, en claro pero autenticada]
//                   [nonce aleatorio 8 B][datos cifrados][MIC 8 B]
//  Trama de ACK:    [cabecera 3 B][MIC 8 B]
//      El MIC del ACK se calcula sobre el nonce del mensaje que confirma:
//      solo quien tiene la clave puede confirmar ese mensaje concreto.
//
//  El nonce es aleatorio (RNG por hardware del ESP32), así que nunca se
//  repite con la misma clave. No hay protección contra repetición (replay).
// ---------------------------------------------------------------------------

#define SEC_NONCE_LEN 8
#define SEC_MIC_LEN   8
#define SEC_OVERHEAD  (SEC_NONCE_LEN + SEC_MIC_LEN)

namespace Secure {
  bool begin();
  // true si se está usando la clave de ejemplo de secrets.h.example
  bool usingExampleKey();

  // Cifra hdr+datos. Devuelve la longitud de la trama (0 si no cabe) y
  // copia el nonce usado en nonceOut (para validar luego el ACK).
  size_t seal(const void* hdr, size_t hdrLen, const void* data, size_t dataLen,
              uint8_t* out, size_t outMax, uint8_t nonceOut[SEC_NONCE_LEN]);
  // Verifica y descifra. false si la trama ha sido alterada o la clave no coincide.
  bool open(const uint8_t* frame, size_t frameLen, size_t hdrLen, void* dataOut,
            size_t dataLen, uint8_t nonceOut[SEC_NONCE_LEN]);

  // ACK autenticado para el mensaje cuyo nonce es reqNonce.
  size_t sealAck(const void* hdr, size_t hdrLen, const uint8_t reqNonce[SEC_NONCE_LEN],
                 uint8_t* out, size_t outMax);
  bool openAck(const uint8_t* frame, size_t frameLen, size_t hdrLen,
               const uint8_t reqNonce[SEC_NONCE_LEN]);
}
