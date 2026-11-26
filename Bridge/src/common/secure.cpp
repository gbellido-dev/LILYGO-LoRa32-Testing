#include "secure.h"
#include <Arduino.h>
#include <string.h>
#include <mbedtls/ccm.h>
#if __has_include(<esp_random.h>)
  #include <esp_random.h>
#else
  #include <esp_system.h>
#endif

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets.h.example"
#endif

#ifndef LORA_KEY
  #error "Define LORA_KEY (16 bytes) en include/secrets.h"
#endif

static const uint8_t kKey[16] = LORA_KEY;
static const uint8_t kExampleKey[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
static mbedtls_ccm_context ccm;

// Nonce CCM de 13 bytes: [dirección][nonce aleatorio 8 B]["LoRa"]
// La dirección separa datos (0) de ACKs (1): aunque el ACK reutilice el
// nonce aleatorio del mensaje que confirma, el nonce CCM es distinto.
enum : uint8_t { DIR_DATA = 0, DIR_ACK = 1 };
static void makeNonce(uint8_t n[13], uint8_t dir, const uint8_t rnd[SEC_NONCE_LEN]) {
  n[0] = dir;
  memcpy(n + 1, rnd, SEC_NONCE_LEN);
  memcpy(n + 9, "LoRa", 4);
}

namespace Secure {

bool begin() {
  mbedtls_ccm_init(&ccm);
  return mbedtls_ccm_setkey(&ccm, MBEDTLS_CIPHER_ID_AES, kKey, 128) == 0;
}

bool usingExampleKey() { return memcmp(kKey, kExampleKey, 16) == 0; }

size_t seal(const void* hdr, size_t hdrLen, const void* data, size_t dataLen,
            uint8_t* out, size_t outMax, uint8_t nonceOut[SEC_NONCE_LEN]) {
  size_t total = hdrLen + SEC_NONCE_LEN + dataLen + SEC_MIC_LEN;
  if (total > outMax) return 0;
  uint8_t* p = out;
  memcpy(p, hdr, hdrLen);              p += hdrLen;
  esp_fill_random(p, SEC_NONCE_LEN);   // RNG hardware
  memcpy(nonceOut, p, SEC_NONCE_LEN);
  uint8_t n[13];
  makeNonce(n, DIR_DATA, p);           p += SEC_NONCE_LEN;
  if (mbedtls_ccm_encrypt_and_tag(&ccm, dataLen, n, sizeof n, out, hdrLen,
                                  (const uint8_t*)data, p, p + dataLen, SEC_MIC_LEN) != 0)
    return 0;
  return total;
}

bool open(const uint8_t* frame, size_t frameLen, size_t hdrLen, void* dataOut,
          size_t dataLen, uint8_t nonceOut[SEC_NONCE_LEN]) {
  if (frameLen != hdrLen + SEC_NONCE_LEN + dataLen + SEC_MIC_LEN) return false;
  const uint8_t* rnd = frame + hdrLen;
  const uint8_t* ct = rnd + SEC_NONCE_LEN;
  uint8_t n[13];
  makeNonce(n, DIR_DATA, rnd);
  if (mbedtls_ccm_auth_decrypt(&ccm, dataLen, n, sizeof n, frame, hdrLen, ct,
                               (uint8_t*)dataOut, ct + dataLen, SEC_MIC_LEN) != 0)
    return false;
  memcpy(nonceOut, rnd, SEC_NONCE_LEN);
  return true;
}

size_t sealAck(const void* hdr, size_t hdrLen, const uint8_t reqNonce[SEC_NONCE_LEN],
               uint8_t* out, size_t outMax) {
  size_t total = hdrLen + SEC_MIC_LEN;
  if (total > outMax) return 0;
  memcpy(out, hdr, hdrLen);
  uint8_t n[13];
  makeNonce(n, DIR_ACK, reqNonce);
  if (mbedtls_ccm_encrypt_and_tag(&ccm, 0, n, sizeof n, out, hdrLen, nullptr, nullptr,
                                  out + hdrLen, SEC_MIC_LEN) != 0)
    return 0;
  return total;
}

bool openAck(const uint8_t* frame, size_t frameLen, size_t hdrLen,
             const uint8_t reqNonce[SEC_NONCE_LEN]) {
  if (frameLen != hdrLen + SEC_MIC_LEN) return false;
  uint8_t n[13];
  makeNonce(n, DIR_ACK, reqNonce);
  return mbedtls_ccm_auth_decrypt(&ccm, 0, n, sizeof n, frame, hdrLen, nullptr, nullptr,
                                  frame + hdrLen, SEC_MIC_LEN) == 0;
}

}  // namespace Secure
