#include "escena.h"
#include <math.h>
#include <stdlib.h>

// ---------------- Ajustes ----------------
static const float VEL_FONDO   = 18.0f;  // px/s del suelo (el pez "avanza" a esta velocidad)
static const float VEL_LEJOS   = 5.0f;   // px/s de las dunas lejanas (cambian poco)
static const float ENCUENTRO_MIN = 6.0f; // s entre animales
static const float ENCUENTRO_MAX = 14.0f;

static const int W = 128, H = 64;
static const float PEZ_YMIN = 9, PEZ_YMAX = 45;   // alto por el que nada el pez

// ---------------- Azar ----------------
static uint32_t rng = 1;
static uint32_t rnd() {               // xorshift32
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
  return rng;
}
static float rndf(float a, float b) { return a + (b - a) * (rnd() & 0xFFFF) / 65535.0f; }
// Hash para decorar el mundo de forma fija (la misma roca siempre en el mismo sitio)
static uint32_t hash(int32_t x) {
  uint32_t h = (uint32_t)x * 2654435761u;
  h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
  return h;
}

// ---------------- Utilidades de dibujo ----------------
static bool fuera(int x0, int x1) { return x1 < 0 || x0 >= W; }

static void pixel(u8g2_t* u, int x, int y) {
  if (x >= 0 && x < W && y >= 0 && y < H) u8g2_DrawPixel(u, x, y);
}
static void linea(u8g2_t* u, int x0, int y0, int x1, int y1) {
  // Bresenham propio: admite coordenadas negativas sin problemas
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    pixel(u, x0, y0);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
// Elipse rellena (o solo la mitad superior/inferior) recortada a pantalla
enum { MITAD_TODA, MITAD_ARRIBA, MITAD_ABAJO };
static void elipse(u8g2_t* u, float cx, float cy, float rx, float ry, int mitad = MITAD_TODA) {
  if (fuera((int)(cx - rx), (int)(cx + rx))) return;
  int y0 = (int)floorf(cy - ry), y1 = (int)ceilf(cy + ry);
  if (mitad == MITAD_ARRIBA) y1 = (int)cy;
  if (mitad == MITAD_ABAJO) y0 = (int)cy;
  for (int y = y0; y <= y1; y++) {
    if (y < 0 || y >= H) continue;
    float d = (y + 0.5f - cy) / ry;
    if (d * d > 1) continue;
    float hw = rx * sqrtf(1 - d * d);
    int xa = (int)lroundf(cx - hw), xb = (int)lroundf(cx + hw);
    if (xa < 0) xa = 0;
    if (xb > W - 1) xb = W - 1;
    if (xb >= xa) u8g2_DrawHLine(u, xa, y, xb - xa + 1);
  }
}
static void contornoElipse(u8g2_t* u, float cx, float cy, float rx, float ry) {
  if (fuera((int)(cx - rx), (int)(cx + rx))) return;
  int n = (int)(rx + ry) * 2;
  for (int i = 0; i < n; i++) {
    float a = 6.2832f * i / n;
    pixel(u, (int)lroundf(cx + rx * cosf(a)), (int)lroundf(cy + ry * sinf(a)));
  }
}
static void triangulo(u8g2_t* u, int x0, int y0, int x1, int y1, int x2, int y2) {
  u8g2_DrawTriangle(u, x0, y0, x1, y1, x2, y2);   // admite int16, recorta solo
}

// ---------------- Escenario ----------------
static float altoSuelo(float wx) {      // y del suelo en la coordenada de mundo wx
  return 55.0f + 3.0f * sinf(wx * 0.045f) + 2.0f * sinf(wx * 0.011f + 1.3f);
}
static float altoDuna(float wx) {
  return 44.0f + 5.0f * sinf(wx * 0.03f) + 3.0f * sinf(wx * 0.071f + 2.0f);
}

static void dibujaSuperficie(u8g2_t* u, float t) {
  for (int x = 0; x < W; x += 2) {
    int y = (int)lroundf(1.5f + 1.2f * sinf(x * 0.15f + t * 2.0f));
    pixel(u, x, y);
  }
}

static void dibujaDunas(u8g2_t* u, float lejos) {
  // Lejanas: línea de puntos y un punteado ralo debajo (sensación de distancia)
  for (int x = 0; x < W; x++) {
    float wx = lejos + x;
    int y = (int)lroundf(altoDuna(wx));
    if (((int)wx & 1) == 0) pixel(u, x, y);
    for (int yy = y + 3; yy < H; yy += 3)
      if ((((int)wx + yy) % 6) == 0) pixel(u, x, yy);
  }
}

static void dibujaAlga(u8g2_t* u, int x, int base, int segs, float t, float fase) {
  float px = x, py = base;
  for (int k = 0; k < segs; k++) {
    float nx = x + sinf(t * 1.8f + fase + k * 0.55f) * (0.6f + k * 0.5f);
    float ny = py - 4;
    linea(u, (int)px, (int)py, (int)nx, (int)ny);
    linea(u, (int)px + 1, (int)py, (int)nx + 1, (int)ny);
    // hojitas alternas
    if (k > 0 && k < segs - 1) {
      int lado = (k & 1) ? 3 : -2;
      pixel(u, (int)nx + lado, (int)ny + 1);
    }
    px = nx; py = ny;
  }
}

// Algas y rocas en celdas fijas del mundo
static void dibujaDecorado(u8g2_t* u, float mundo, float t, bool algas) {
  const int CELDA = algas ? 23 : 41;
  int c0 = (int)floorf(mundo / CELDA) - 1;
  for (int c = c0; c <= c0 + W / CELDA + 2; c++) {
    uint32_t h = hash(c * (algas ? 7 : 13) + (algas ? 1 : 99));
    if ((h & 3) == 0) continue;                 // algunas celdas vacías
    float wx = c * CELDA + (h >> 8) % (CELDA - 6);
    int x = (int)lroundf(wx - mundo);
    if (x < -20 || x > W + 20) continue;
    int base = (int)altoSuelo(wx) + 1;
    if (algas) {
      int segs = 3 + (h >> 4) % 6;
      dibujaAlga(u, x, base, segs, t, (h % 100) * 0.06f);
      if (h & 0x100) dibujaAlga(u, x + 4, base, segs - 2 > 2 ? segs - 2 : 2, t, (h % 77) * 0.08f);
    } else {
      float rx = 5 + (h >> 4) % 6, ry = 3 + (h >> 9) % 4;
      elipse(u, x, base, rx, ry, MITAD_ARRIBA);
      u8g2_SetDrawColor(u, 0);                  // textura de la roca
      pixel(u, x - 2, base - 2); pixel(u, x + 2, base - (int)ry + 2); pixel(u, x + 1, base - 1);
      u8g2_SetDrawColor(u, 1);
    }
  }
}

static void dibujaSuelo(u8g2_t* u, float mundo) {
  for (int x = 0; x < W; x++) {
    float wx = mundo + x;
    int y = (int)lroundf(altoSuelo(wx));
    u8g2_DrawVLine(u, x, y, H - y);
    // textura de arena: huecos negros sueltos
    u8g2_SetDrawColor(u, 0);
    int iwx = (int)floorf(wx);
    for (int yy = y + 2; yy < H; yy++)
      if (hash(iwx * 131 + yy * 7919) % 9 == 0) u8g2_DrawPixel(u, x, yy);
    u8g2_SetDrawColor(u, 1);
  }
}

// ---------------- Pez protagonista ----------------
static float pezY = 28, pezVy = 0;

static void dibujaPez(u8g2_t* u, float x, float y, float t) {
  float aleteo = sinf(t * 9.0f);
  int ix = (int)lroundf(x), iy = (int)lroundf(y);
  // cola (detrás del cuerpo)
  int colaY = (int)lroundf(aleteo * 2.5f);
  // silueta negra un poco mayor: separa al pez de las algas y del fondo
  u8g2_SetDrawColor(u, 0);
  triangulo(u, ix - 6, iy, ix - 17, iy - 8 + colaY, ix - 17, iy + 8 + colaY);
  triangulo(u, ix - 4, iy - 3, ix + 5, iy - 5, ix - 6, iy - 11);
  elipse(u, x, y, 12, 8);
  u8g2_SetDrawColor(u, 1);
  triangulo(u, ix - 7, iy, ix - 15, iy - 6 + colaY, ix - 15, iy + 6 + colaY);
  // aletas dorsal y ventral
  triangulo(u, ix - 3, iy - 4, ix + 3, iy - 5, ix - 5, iy - 9);
  triangulo(u, ix - 1, iy + 4, ix + 2, iy + 4, ix - 3, iy + 7 + (int)lroundf(aleteo));
  // cuerpo
  elipse(u, x, y, 10, 6);
  // detalles en negro
  u8g2_SetDrawColor(u, 0);
  for (int k = -1; k <= 1; k++) pixel(u, ix + 4, iy + k);        // agalla
  pixel(u, ix + 3, iy - 2); pixel(u, ix + 3, iy + 2);
  linea(u, ix - 4, iy - 3, ix - 4, iy + 3);                      // franja
  linea(u, ix - 1, iy - 4, ix - 1, iy + 4);
  u8g2_DrawBox(u, ix + 6, iy - 2, 2, 2);                         // ojo
  pixel(u, ix + 10, iy + 1);                                     // boca
  u8g2_SetDrawColor(u, 1);
  pixel(u, ix + 7, iy - 2);                                      // brillo del ojo
}

// ---------------- Burbujas ----------------
struct Burbuja { float x, y, r, fase; bool viva; };
static Burbuja burbujas[10];
static float proxBurbuja = 1;

static void sueltaBurbuja(float x, float y) {
  for (auto& b : burbujas) if (!b.viva) { b = {x, y, rndf(0.8f, 2.2f), rndf(0, 6.3f), true}; return; }
}

static void actualizaBurbujas(float dt, float t) {
  for (auto& b : burbujas) {
    if (!b.viva) continue;
    b.y -= (10 + b.r * 4) * dt;
    b.x -= VEL_FONDO * 0.6f * dt;          // se quedan atrás: el pez avanza
    if (b.y < 3 || b.x < -4) b.viva = false;
  }
  (void)t;
}
static void dibujaBurbujas(u8g2_t* u, float t) {
  for (auto& b : burbujas) {
    if (!b.viva) continue;
    float x = b.x + sinf(t * 3 + b.fase) * 1.2f;
    if (b.r < 1.2f) pixel(u, (int)lroundf(x), (int)lroundf(b.y));
    else contornoElipse(u, x, b.y, b.r, b.r);
  }
}

// ---------------- Animales que se cruzan ----------------
enum Tipo { MEDUSA, TORTUGA, BANCO, CANGREJO, TIBURON, N_TIPOS };
struct Animal { Tipo tipo; float x, y, vx, t0; bool activo; };
static Animal ani = {MEDUSA, 0, 0, 0, 0, false};
static float proxAnimal = 4;
static int ultimoTipo = -1;

static void dibujaMedusa(u8g2_t* u, float x, float y, float t) {
  float pulso = sinf(t * 3.0f);
  float rx = 8 + pulso * 1.2f, ry = 6 - pulso * 1.0f;
  elipse(u, x, y, rx, ry, MITAD_ARRIBA);
  u8g2_SetDrawColor(u, 0);
  pixel(u, (int)x - 3, (int)y - 3); pixel(u, (int)x + 2, (int)y - 4); pixel(u, (int)x, (int)y - 2);
  u8g2_SetDrawColor(u, 1);
  // tentáculos ondulantes
  for (int k = 0; k < 4; k++) {
    float bx = x - rx + 2 + k * (2 * rx - 4) / 3;
    int px = (int)bx, py = (int)y + 1;
    for (int s = 1; s <= 5; s++) {
      int nx = (int)lroundf(bx + sinf(t * 4 + k + s * 0.9f) * 1.6f);
      int ny = (int)y + 1 + s * 3;
      linea(u, px, py, nx, ny);
      px = nx; py = ny;
    }
  }
}

static void dibujaTortuga(u8g2_t* u, float x, float y, float t) {
  float rem = sinf(t * 4.0f);
  int ix = (int)x, iy = (int)y;
  // aletas (nada hacia la izquierda)
  triangulo(u, ix - 4, iy + 1, ix - 12, iy + 6 + (int)(rem * 3), ix - 1, iy + 3);
  triangulo(u, ix + 6, iy + 1, ix + 13, iy + 5 - (int)(rem * 2), ix + 9, iy + 1);
  // cabeza
  elipse(u, x - 14, y - 1, 4, 3);
  u8g2_SetDrawColor(u, 0); pixel(u, ix - 16, iy - 2); u8g2_SetDrawColor(u, 1);
  // caparazón: relleno + dibujo de placas en negro
  elipse(u, x, y, 11, 8, MITAD_ARRIBA);
  u8g2_DrawHLine(u, ix - 11 < 0 ? 0 : ix - 11, iy + 1, 23);
  u8g2_SetDrawColor(u, 0);
  contornoElipse(u, x, y - 3, 4, 2.5f);
  linea(u, ix - 4, iy - 3, ix - 9, iy - 1);
  linea(u, ix + 4, iy - 3, ix + 9, iy - 1);
  linea(u, ix, iy - 6, ix, iy - 8);
  u8g2_DrawHLine(u, ix - 10 < 0 ? 0 : ix - 10, iy, 21);
  u8g2_SetDrawColor(u, 1);
  // colita
  triangulo(u, ix + 11, iy, ix + 15, iy + 1, ix + 11, iy + 2);
}

static void pezPequeno(u8g2_t* u, float x, float y, float t) {   // mira a la izquierda
  int ix = (int)x, iy = (int)y;
  elipse(u, x, y, 4, 2);
  int c = (int)lroundf(sinf(t * 12) * 1.5f);
  triangulo(u, ix + 3, iy, ix + 7, iy - 3 + c, ix + 7, iy + 3 + c);
  u8g2_SetDrawColor(u, 0); pixel(u, ix - 2, iy - 1); u8g2_SetDrawColor(u, 1);
}

static void dibujaBanco(u8g2_t* u, float x, float y, float t) {
  static const int8_t off[7][2] = {{0, 0}, {11, -6}, {12, 6}, {23, -11}, {24, 0}, {25, 10}, {36, -4}};
  for (int i = 0; i < 7; i++)
    pezPequeno(u, x + off[i][0] + sinf(t * 2 + i) * 1.5f, y + off[i][1] + sinf(t * 1.5f + i * 1.7f) * 1.5f,
               t + i * 0.3f);
}

static void dibujaCangrejo(u8g2_t* u, float x, float y, float t) {
  int ix = (int)x, iy = (int)y;
  float p = sinf(t * 14);
  // borde negro para que no se funda con la arena
  u8g2_SetDrawColor(u, 0);
  elipse(u, x, y - 1, 9, 6);
  elipse(u, x - 11, y - 8, 4, 3.5f);
  elipse(u, x + 11, y - 8, 4, 3.5f);
  u8g2_SetDrawColor(u, 1);
  // patas
  for (int k = 0; k < 3; k++) {
    int a = (int)lroundf(p * ((k & 1) ? 1 : -1));
    linea(u, ix - 3 - k * 2, iy, ix - 7 - k * 2 + a, iy + 4);
    linea(u, ix + 3 + k * 2, iy, ix + 7 + k * 2 - a, iy + 4);
  }
  // cuerpo
  elipse(u, x, y - 1, 7, 4);
  // ojos con pedúnculo
  linea(u, ix - 2, iy - 4, ix - 3, iy - 8); linea(u, ix + 2, iy - 4, ix + 3, iy - 8);
  elipse(u, x - 3, y - 9, 1.2f, 1.2f); elipse(u, x + 3, y - 9, 1.2f, 1.2f);
  // pinzas que se abren y cierran
  int ab = (p > 0) ? 2 : 0;
  linea(u, ix - 6, iy - 2, ix - 10, iy - 6);
  linea(u, ix + 6, iy - 2, ix + 10, iy - 6);
  elipse(u, x - 11, y - 8, 2.5f, 2);
  elipse(u, x + 11, y - 8, 2.5f, 2);
  u8g2_SetDrawColor(u, 0);
  linea(u, ix - 13, iy - 8, ix - 11, iy - 8 + ab);
  linea(u, ix + 13, iy - 8, ix + 11, iy - 8 + ab);
  pixel(u, ix - 2, iy - 2); pixel(u, ix + 2, iy - 2);   // boca/sonrisa
  pixel(u, ix - 1, iy - 1); pixel(u, ix, iy - 1); pixel(u, ix + 1, iy - 1);
  u8g2_SetDrawColor(u, 1);
}

static void dibujaTiburon(u8g2_t* u, float x, float y, float t) {   // mira a la izquierda
  int ix = (int)x, iy = (int)y;
  float c = sinf(t * 5) * 2.5f;
  // tapa lo que hay detrás (silueta en negro) y luego contorno
  u8g2_SetDrawColor(u, 0);
  elipse(u, x, y, 23, 6);
  u8g2_SetDrawColor(u, 1);
  // aleta dorsal, pectoral y cola
  triangulo(u, ix - 4, iy - 5, ix + 6, iy - 5, ix + 4, iy - 14);
  triangulo(u, ix - 8, iy + 4, ix - 2, iy + 4, ix + 1, iy + 10);
  triangulo(u, ix + 20, iy, ix + 31, iy - 10 + (int)c, ix + 28, iy + (int)c);
  triangulo(u, ix + 20, iy, ix + 28, iy + (int)c, ix + 29, iy + 7 + (int)c);
  // cuerpo: vientre claro (mitad de abajo rellena), lomo en contorno
  elipse(u, x, y, 23, 6, MITAD_ABAJO);
  contornoElipse(u, x, y, 23, 6);
  contornoElipse(u, x, y, 22.5f, 5.5f);
  u8g2_SetDrawColor(u, 0);
  for (int k = 0; k < 3; k++) linea(u, ix - 12 + k * 2, iy + 1, ix - 13 + k * 2, iy + 4);   // agallas
  u8g2_DrawHLine(u, ix - 22 < 0 ? 0 : ix - 22, iy + 2, 6);                               // boca
  u8g2_SetDrawColor(u, 1);
  u8g2_DrawBox(u, ix - 17 < 0 ? 0 : ix - 17, iy - 2, 2, 2);                               // ojo
}

static int pedido = -2;   // -2: nada pedido · -1: cualquiera · 0..N-1: ese tipo

static void nuevoAnimal(float t) {
  int tipo;
  if (pedido >= 0 && pedido < N_TIPOS) tipo = pedido;
  else do { tipo = rnd() % N_TIPOS; } while (tipo == ultimoTipo);
  pedido = -2;
  ultimoTipo = tipo;
  Animal a = {(Tipo)tipo, W + 30.0f, 0, 0, t, true};
  switch (a.tipo) {
    // Alturas elegidas para que siempre quede hueco al pez por arriba o por abajo
    case MEDUSA:   a.y = rndf(8, 12);  a.vx = -VEL_FONDO * 0.8f; break;   // va a la deriva, arriba
    case TORTUGA:  a.y = (rnd() & 1) ? rndf(15, 24) : rndf(33, 37);
                   a.vx = -VEL_FONDO - 12; break;                         // nada hacia el pez
    case BANCO:    a.y = rndf(14, 22); a.vx = -VEL_FONDO - 22;  a.x = W + 10; break;
    case CANGREJO: a.y = 0;            a.vx = -VEL_FONDO + 4;   break;    // camina por el suelo
    case TIBURON:  a.y = rndf(16, 24); a.vx = -VEL_FONDO - 30;  a.x = W + 35; break;
    default: break;
  }
  ani = a;
}

static void actualizaAnimal(float t, float dt, float mundo) {
  if (!ani.activo) {
    if (t >= proxAnimal || pedido != -2) nuevoAnimal(t);
    return;
  }
  ani.x += ani.vx * dt;
  if (ani.tipo == MEDUSA) ani.y += sinf((t - ani.t0) * 1.5f) * 4 * dt;
  if (ani.tipo == CANGREJO) ani.y = altoSuelo(mundo + ani.x) - 4;
  if (ani.x < -50) {
    ani.activo = false;
    proxAnimal = t + rndf(ENCUENTRO_MIN, ENCUENTRO_MAX);
  }
}

static void dibujaAnimal(u8g2_t* u, float t) {
  if (!ani.activo) return;
  switch (ani.tipo) {
    case MEDUSA:   dibujaMedusa(u, ani.x, ani.y, t); break;
    case TORTUGA:  dibujaTortuga(u, ani.x, ani.y, t); break;
    case BANCO:    dibujaBanco(u, ani.x, ani.y, t); break;
    case CANGREJO: dibujaCangrejo(u, ani.x, ani.y, t); break;
    case TIBURON:  dibujaTiburon(u, ani.x, ani.y, t); break;
    default: break;
  }
}

// Altura (centro) que ocupa el animal, para que el pez lo esquive
static bool zonaAnimal(float& cy, float& x0, float& x1, float& radio) {
  if (!ani.activo || ani.tipo == CANGREJO) return false;
  switch (ani.tipo) {
    case MEDUSA:  cy = ani.y + 5; radio = 12; x0 = ani.x - 9;  x1 = ani.x + 9;  break;
    case TORTUGA: cy = ani.y - 1; radio = 10; x0 = ani.x - 18; x1 = ani.x + 15; break;
    case BANCO:   cy = ani.y - 1; radio = 13; x0 = ani.x - 5;  x1 = ani.x + 43; break;
    case TIBURON: cy = ani.y - 2; radio = 12; x0 = ani.x - 23; x1 = ani.x + 31; break;
    default: return false;
  }
  return true;
}

void escena_llama_animal(int tipo) { pedido = tipo; }

// ---------------- Bucle de la escena ----------------
static float tPrev = -1;

void escena_init(uint32_t semilla) {
  rng = semilla ? semilla : 0x12345678;
  for (auto& b : burbujas) b.viva = false;
  ani.activo = false;
  proxAnimal = 4;
  pedido = -2;
  tPrev = -1;
}

void escena_dibuja(u8g2_t* u, float t) {
  float dt = tPrev < 0 ? 0 : t - tPrev;
  if (dt > 0.2f) dt = 0.2f;
  tPrev = t;
  float mundo = t * VEL_FONDO, lejos = t * VEL_LEJOS;

  // Pez: deambula arriba y abajo, y se aparta si un animal se le acerca
  float pezX = 40 + 4 * sinf(t * 0.23f);
  float objetivo = 28 + 9 * sinf(t * 0.37f) + 4 * sinf(t * 0.91f + 1);
  float cy, x0, x1, radio;
  bool esquiva = false;
  // Mira por delante: cuanto más rápido viene el animal, antes reacciona
  if (zonaAnimal(cy, x0, x1, radio) && x0 < pezX + 25 + fabsf(ani.vx) * 1.8f &&
      x1 > pezX - 25 && fabsf(cy - pezY) < radio + 8) {
    esquiva = true;
    float arriba = cy - radio - 9, abajo = cy + radio + 9;
    bool cabeArriba = arriba >= PEZ_YMIN, cabeAbajo = abajo <= PEZ_YMAX;
    bool estaArriba = pezY < cy;
    bool lejos = x0 > pezX + 15 + fabsf(ani.vx);   // aún da tiempo a cambiar de lado
    if (estaArriba) objetivo = (cabeArriba || !cabeAbajo || !lejos) ? arriba : abajo;
    else            objetivo = (cabeAbajo || !cabeArriba || !lejos) ? abajo : arriba;
  }
  if (objetivo < PEZ_YMIN) objetivo = PEZ_YMIN;
  if (objetivo > PEZ_YMAX) objetivo = PEZ_YMAX;
  // muelle amortiguado: suave al pasear, más brusco al esquivar
  float k = esquiva ? 25.0f : 4.0f, amort = esquiva ? 8.0f : 3.0f;
  pezVy += ((objetivo - pezY) * k - pezVy * amort) * dt;
  pezY += pezVy * dt;

  if (t >= proxBurbuja) {
    sueltaBurbuja(pezX + 11, pezY - 1);
    proxBurbuja = t + rndf(0.6f, 2.5f);
  }
  actualizaBurbujas(dt, t);
  actualizaAnimal(t, dt, mundo);

  // Dibujo, de atrás hacia delante
  u8g2_SetDrawColor(u, 1);
  dibujaSuperficie(u, t);
  dibujaDunas(u, lejos);
  dibujaDecorado(u, mundo, t, false);       // rocas
  if (ani.activo && ani.tipo == TIBURON) dibujaAnimal(u, t);   // el tiburón pasa por detrás
  dibujaDecorado(u, mundo, t, true);        // algas
  dibujaBurbujas(u, t);
  if (ani.activo && ani.tipo != TIBURON && ani.tipo != CANGREJO) dibujaAnimal(u, t);
  dibujaPez(u, pezX, pezY, t);
  dibujaSuelo(u, mundo);
  if (ani.activo && ani.tipo == CANGREJO) dibujaAnimal(u, t);   // camina sobre la arena
}
