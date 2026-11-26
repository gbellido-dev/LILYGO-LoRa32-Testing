#include "display.h"
#include "config.h"
#include <U8g2lib.h>
#include <Wire.h>

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(
    U8G2_R0, OLED_RST < 0 ? U8X8_PIN_NONE : OLED_RST, OLED_SCL, OLED_SDA);

namespace Display {

void begin() {
  oled.begin();
  oled.setFont(u8g2_font_6x10_tf);
}

void show(const char* l0, const char* l1, const char* l2, const char* l3,
          const char* l4, const char* l5) {
  const char* lines[6] = {l0, l1, l2, l3, l4, l5};
  oled.clearBuffer();
  for (int i = 0; i < 6; i++)
    if (lines[i]) oled.drawStr(0, 10 + i * 10, lines[i]);
  oled.sendBuffer();
}

}  // namespace Display
