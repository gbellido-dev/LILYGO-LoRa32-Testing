#pragma once
// ---------------------------------------------------------------------------
//  Configuración común a los dos nodos. Ambos DEBEN compilarse con los mismos
//  valores de frecuencia, sync word y canal de control.
// ---------------------------------------------------------------------------

// ---------------- Pines ----------------
#if defined(BOARD_LORA32_V1)
  #define LORA_SCK   5
  #define LORA_MISO 19
  #define LORA_MOSI 27
  #define LORA_CS   18
  #define LORA_RST  14
  #define LORA_DIO0 26
  #define LORA_DIO1 33   // en V1.0 hay que puentearlo; no se usa
  #define OLED_SDA   4
  #define OLED_SCL  15
  #define OLED_RST  16
  #define LED_PIN    2
#else  // LoRa32 V2.1 / T3 V1.6.x (por defecto)
  #define LORA_SCK   5
  #define LORA_MISO 19
  #define LORA_MOSI 27
  #define LORA_CS   18
  #define LORA_RST  23
  #define LORA_DIO0 26
  #define LORA_DIO1 33
  #define OLED_SDA  21
  #define OLED_SCL  22
  #define OLED_RST  -1
  #define LED_PIN   25
#endif
#define BUTTON_PIN 0     // botón BOOT/IO0: en casa lanza un barrido por defecto

// ---------------- Radio ----------------
// 869.525 MHz está en la sub-banda g3 (869.40–869.65 MHz) de la UE:
// ciclo de trabajo 10 % y hasta 27 dBm ERP. Con 868.1 MHz el límite es 1 %
// y un barrido con SF12 tardaría muchísimo.
#ifndef LORA_FREQ_MHZ
#define LORA_FREQ_MHZ   869.525
#endif
#define LORA_SYNC_WORD  0x12      // privada (no LoRaWAN)
#define LORA_PREAMBLE   8

// Ciclo de trabajo que respeta cada nodo (0.10 = 10 %).
#ifndef DUTY_CYCLE
#define DUTY_CYCLE      0.10f
#endif

// Canal de control: siempre el más robusto para que la negociación llegue
// aunque el SF bajo a probar no llegue.
#define CTRL_SF     12
#define CTRL_BW     125.0
#define CTRL_CR     5
#define CTRL_POWER  17   // dBm (máx. SX1276 PA_BOOST = 20)
