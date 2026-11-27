#pragma once
// ---------------------------------------------------------------------------
//  Escena del acuario. Usa solo la API en C de U8g2, así que el mismo código
//  sirve en la placa y en el ordenador (para generar la vista previa).
// ---------------------------------------------------------------------------
#include <stdint.h>
#include "u8g2.h"

// Prepara la escena. Con la misma semilla sale siempre la misma secuencia.
void escena_init(uint32_t semilla);
// Avanza la animación hasta el instante t (segundos) y dibuja el fotograma en el búfer.
void escena_dibuja(u8g2_t* u, float t);

// Hace salir un animal en cuanto no haya otro en pantalla.
// tipo: -1 cualquiera · 0 medusa · 1 tortuga · 2 banco de peces · 3 cangrejo · 4 tiburón
void escena_llama_animal(int tipo);
