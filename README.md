# LILYGO LoRa32: garaje ↔ casa

Dos placas LILYGO LoRa32 (SX1276, 868 MHz): una en casa (WiFi) y otra en el garaje (solo alimentación).
El repositorio tiene **varios proyectos PlatformIO independientes**. Abre cada carpeta por separado en VS Code
o usa `pio` dentro de ella.

| Carpeta | Para qué | Envs |
|---------|----------|------|
| [`Testing/`](Testing) | Medir el enlace: barridos de SF7…SF12 con ping/pong, resultados en Node-RED (dashboard + CSV) | `home`, `garage` (+ `_v1`) |
| [`Alfabeto/`](Alfabeto) | Prueba sencilla del OLED: una letra mayúscula cada 3 s, a 58 px de alto | `alfabeto` (+ `_v1`) |
| [`Acuario/`](Acuario) | Animación en el OLED: un pez que avanza por el fondo del mar y se cruza con otros animales | `acuario` (+ `_v1`) |
| [`Bridge/`](Bridge) | Uso definitivo: PIR, puerta y BME280 del garaje → LoRa **cifrado (AES-128)** → MQTT → **Home Assistant** (autodescubrimiento y detección de inhibidores) | `home`, `garage` (+ `_v1`) |

## Orden recomendado

1. **Testing**: graba las dos placas, haz barridos y anota el SF más bajo con ~100 % de entrega
   en los dos sentidos y un margen de SNR de 5-10 dB.
2. **Bridge**: pon ese SF en `Bridge/include/config.h` (`LINK_SF`), conecta los sensores y graba las dos placas.

Los dos proyectos usan el mismo hardware y la misma frecuencia (869.525 MHz, 10 % de ciclo de trabajo),
pero distinto identificador de paquete: no se confunden si alguna vez conviven.

```bash
cd Testing && pio run -e garage -t upload    # dentro de cada carpeta
pio run -e bridge_garage -t upload           # o desde la raíz: testing_*, bridge_*, alfabeto, acuario
```

En las dos carpetas tienes que copiar `include/secrets.h.example` como `include/secrets.h`.
