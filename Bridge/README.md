# Bridge: sensores del garaje → LoRa → Home Assistant

```
[PIR · puerta · BME280] ── LoRa32 garaje ~~~ LoRa ~~~ LoRa32 casa ── WiFi/MQTT ── Home Assistant
```

| Nodo | Dónde | Qué hace |
|------|-------|----------|
| `home`   | Casa: WiFi + MQTT | Confirma los mensajes del garaje y los publica en HA con MQTT Discovery |
| `garage` | Garaje: solo alimentación | Lee el PIR, la puerta y el BME280 y los envía por LoRa |

## Firmware (PlatformIO)

```bash
cd Bridge
cp include/secrets.h.example include/secrets.h   # WiFi y broker MQTT
pio run -e garage -t upload     # placa del garaje
pio run -e home   -t upload     # placa de casa
pio device monitor              # 115200 baudios
```

Los dos nodos tienen que compilarse con la misma frecuencia y los mismos `LINK_*` (`include/config.h`).
Elige `LINK_SF` con el proyecto [Testing](../Testing).

## Hardware

Por defecto se compila para la **LoRa32 V2.1 (T3 V1.6.x)**. Para la V1.0 usa los entornos `home_v1` / `garage_v1`.
Los pines están en `include/config.h`. **No enciendas nunca las placas sin antena.**

## Sensores del garaje

Cada sensor es opcional: pon `-1` en su pin (o `BME280_ADDR 0`) en `include/config.h` para desactivarlo.
Casa solo anuncia en HA los sensores que el garaje tiene.

| Sensor | Conexión (V2.1) | Notas |
|--------|-----------------|-------|
| PIR (HC-SR501 o AM312) | VCC → 5V (HC-SR501) o 3V3 (AM312) · GND · OUT → **GPIO34** | La salida del HC-SR501 es de 3.3 V: se conecta directa |
| Contacto de puerta (reed) | Un cable → **GPIO13**, el otro → GND | Usa el pull-up interno. Con el imán junto al reed: cerrado = puerta cerrada |
| BME280 (temperatura, humedad, presión) | VCC → 3V3 · GND · SDA → **21** · SCL → **22** | Comparte el I2C con el OLED. Dirección 0x76 (algunos módulos usan 0x77). En la V1.0: SDA 4 y SCL 15 |

En la V2.1, el GPIO13 también es el CS de la ranura microSD: no pongas tarjeta, o cambia `DOOR_PIN`
(por ejemplo, a 4, 12 o 15).

## Cómo se envían los sensores

- El garaje envía su estado completo cuando cambia el movimiento o la puerta, cuando la temperatura varía ≥ 0.5 °C,
  y como latido cada `HEARTBEAT_S` (60 s).
- Casa responde con un ACK. Sin ACK, el garaje reintenta hasta 4 veces, descansa 15 s y vuelve a probar.
- Un movimiento corto nunca se pierde: el garaje lo retiene hasta que casa lo confirma,
  y además cuenta las activaciones (`motion_events`).
- Si casa no oye al garaje en 3 latidos (~3.5 min), el garaje pasa a **no disponible** en HA.
  Lo mismo ocurre si el nodo de casa se desconecta (LWT).
- Si casa no puede contestar en ese momento por el ciclo de trabajo, publica igualmente
  y el garaje reenvía el mensaje.

**Latencia:** depende de `LINK_SF`. Con SF12, un aviso de movimiento tarda unos 1.2 s y, por el
ciclo de trabajo, puede haber unos 10 s entre mensajes seguidos. Con SF9 baja a ~0.2 s y ~2 s.
Por eso, **cuando tengas los resultados del proyecto [Testing](../Testing), baja `LINK_SF`** al SF más bajo con ~100 % de
entrega y margen de SNR, y graba de nuevo las **dos** placas.

## Home Assistant

1. Integración **MQTT** de HA apuntando al mismo broker que usa el nodo de casa.
2. Graba las dos placas. Con el primer mensaje del garaje aparecen dos dispositivos:
   - **Garaje (LoRa)**: Movimiento, Puerta, Temperatura, Humedad, Presión y, como diagnóstico,
     Activaciones PIR, RSSI y SNR de los dos sentidos y Tiempo encendido.
   - **Puente LoRa (casa)**: conectividad del nodo de casa.
3. Las entidades se llaman `binary_sensor.garaje_motion`, `binary_sensor.garaje_door`,
   `sensor.garaje_temperature`, etc. Úsalas en automatizaciones como cualquier otra.

El prefijo de descubrimiento es `homeassistant`; si usas otro, cambia `HA_PREFIX` con `-DHA_PREFIX=...`.

## MQTT

| Topic | Contenido |
|-------|-----------|
| `lora32/status` | `online` / `offline` del nodo de casa (retenido, LWT) |
| `lora32/garage/state` | `{"motion":false,"door_open":false,"temperature":14.2,"humidity":61,"pressure":1013.4,"motion_events":12,"rssi":-112,"snr":-3.5,"rssi_up":-110,"uptime_min":340}` (retenido) |
| `lora32/garage/availability` | `online` / `offline` (retenido) |
| `homeassistant/.../config` | MQTT Discovery (retenido) |
