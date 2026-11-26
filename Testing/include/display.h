#pragma once
#include <Arduino.h>

namespace Display {
  void begin();
  // Hasta 6 líneas de ~21 caracteres. Las nulas no se pintan.
  void show(const char* l0, const char* l1 = nullptr, const char* l2 = nullptr,
            const char* l3 = nullptr, const char* l4 = nullptr,
            const char* l5 = nullptr);
}
