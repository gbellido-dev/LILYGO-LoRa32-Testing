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

// ---------------- Radio ----------------
// 869.525 MHz está en la sub-banda g3 (869.40–869.65 MHz) de la UE:
// ciclo de trabajo 10 % y hasta 27 dBm ERP. Con 868.1 MHz el límite es 1 %
// y con SF altos habría mucha espera entre mensajes.
#ifndef LORA_FREQ_MHZ
#define LORA_FREQ_MHZ   869.525
#endif
#define LORA_SYNC_WORD  0x12      // privada (no LoRaWAN)
#define LORA_PREAMBLE   8

// Ciclo de trabajo que respeta cada nodo (0.10 = 10 %).
#ifndef DUTY_CYCLE
#define DUTY_CYCLE      0.10f
#endif

// Parámetros del ENLACE garaje <-> casa. Iguales en los DOS nodos.
// Elige el SF con el proyecto Testing: el más bajo con ~100 % de entrega y
// SNR >= límite + 5 dB. Más SF = más alcance, pero más latencia y espera.
//   Mensaje cifrado: SF7 ~ 70 ms · SF9 ~ 250 ms · SF12 ~ 1.8 s
#ifndef LINK_SF
#define LINK_SF     12
#endif
#define LINK_BW     125.0
#define LINK_CR     5
#define LINK_POWER  17   // dBm (máx. SX1276 PA_BOOST = 20)

// ---------------- Sensores del garaje ----------------
// Pon -1 en un pin para desactivar ese sensor.
// PIR (HC-SR501 a 5 V o AM312 a 3.3 V): salida a GPIO34 (solo entrada).
#define PIR_PIN         34
#define PIR_ACTIVE      HIGH
// Contacto de puerta (reed): entre GPIO13 y GND, pull-up interno.
// Imán junto al reed = contacto cerrado = LOW = puerta cerrada.
#define DOOR_PIN        13
#define DOOR_OPEN_LEVEL HIGH
// BME280 (temperatura/humedad/presión) en el mismo I2C que el OLED.
// 0 = desactivado. Dirección 0x76 (o 0x77 según el módulo).
#define BME280_ADDR     0x76

// Envío periódico del estado completo (s). Casa marca el garaje como
// "no disponible" en Home Assistant si no oye nada en 3 periodos.
#define HEARTBEAT_S     60
// Además se envía si la temperatura cambia más de esto (°C).
#define TEMP_DELTA      0.5f
