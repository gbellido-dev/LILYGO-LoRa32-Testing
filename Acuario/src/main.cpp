// ---------------------------------------------------------------------------
//  Acuario: un pez que avanza sin parar por el fondo del mar, con algas,
//  rocas y dunas en paralaje, y animales que se cruzan de vez en cuando
//  (medusa, tortuga, banco de peces, cangrejo y tiburón).
//  Toda la escena está en escena.cpp.
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <U8g2lib.h>
#include "escena.h"

#if defined(BOARD_LORA32_V1)
  #define OLED_SDA  4
  #define OLED_SCL 15
  #define OLED_RST 16
#else  // V2.1 / T3 V1.6.x
  #define OLED_SDA 21
  #define OLED_SCL 22
  #define OLED_RST U8X8_PIN_NONE
#endif

#define FPS 30
#define BOTON 0      // BOOT: hace salir un animal

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

void setup() {
  Serial.begin(115200);
  oled.setBusClock(400000);   // I2C rápido: ~30 fotogramas por segundo
  oled.begin();
  escena_init(esp_random());  // cada arranque, otra secuencia de animales
  pinMode(BOTON, INPUT_PULLUP);
}

void loop() {
  static uint32_t siguiente = 0;
  uint32_t ahora = millis();
  if ((int32_t)(ahora - siguiente) < 0) { delay(1); return; }
  siguiente = ahora + 1000 / FPS;
  static bool antes = false;
  bool pulsado = digitalRead(BOTON) == LOW;
  if (pulsado && !antes) escena_llama_animal(-1);
  antes = pulsado;
  oled.clearBuffer();
  escena_dibuja(oled.getU8g2(), ahora / 1000.0f);
  oled.sendBuffer();
}
