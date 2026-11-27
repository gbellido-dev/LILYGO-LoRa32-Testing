// ---------------------------------------------------------------------------
//  Alfabeto: una letra mayúscula cada 3 segundos en el OLED, lo más grande
//  posible (fuente Logisoso de 58 px de alto en una pantalla de 64).
// ---------------------------------------------------------------------------
#include <Arduino.h>
#include <U8g2lib.h>

#if defined(BOARD_LORA32_V1)
  #define OLED_SDA  4
  #define OLED_SCL 15
  #define OLED_RST 16
#else  // V2.1 / T3 V1.6.x
  #define OLED_SDA 21
  #define OLED_SCL 22
  #define OLED_RST U8X8_PIN_NONE
#endif

// Cambia esto para ajustar el ritmo o quitar la Ñ.
#define INTERVALO_MS 3000
static const char* LETRAS = "ABCDEFGHIJKLMN\x01OPQRSTUVWXYZ";  // \x01 = Ñ

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

// La fuente grande no tiene Ñ: se dibuja una N de 50 px y una virgulilla encima.
static void dibujaEnie() {
  oled.setFont(u8g2_font_logisoso50_tr);
  int w = oled.getStrWidth("N");
  int x0 = (128 - w) / 2;
  oled.drawStr(x0, 63, "N");
  int tw = w - 4, tx = x0 + 2;
  for (int i = 0; i <= tw; i++) {
    float yc = 5.5f - 3.0f * sinf(2 * PI * i / tw);
    oled.drawVLine(tx + i, (int)lroundf(yc - 2), 5);
  }
}

static void dibujaLetra(char c) {
  oled.clearBuffer();
  if (c == '\x01') {
    dibujaEnie();
  } else {
    char s[2] = {c, 0};
    oled.setFont(u8g2_font_logisoso58_tr);
    oled.drawStr((128 - oled.getStrWidth(s)) / 2, 61, s);  // centrada
  }
  oled.sendBuffer();
}

void setup() {
  Serial.begin(115200);
  oled.begin();
}

void loop() {
  static size_t i = 0;
  char c = LETRAS[i];
  Serial.printf("Letra: %s\n", c == '\x01' ? "Ñ" : String(c).c_str());
  dibujaLetra(c);
  i = (i + 1) % strlen(LETRAS);
  delay(INTERVALO_MS);
}
