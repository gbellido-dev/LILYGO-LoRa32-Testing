# Bridge: sensores del garaje → LoRa → Home Assistant

```
[PIR · puerta · BME280] ── LoRa32 garaje ~~~ LoRa ~~~ LoRa32 casa ── WiFi/MQTT ── Home Assistant
```

| Nodo | Dónde | Qué hace |
|------|-------|----------|
| `home`   | Casa: WiFi + MQTT | Verifica y confirma los mensajes del garaje, los publica en HA con MQTT Discovery y vigila el ruido del canal |
| `garage` | Garaje: solo alimentación | Lee el PIR, la puerta y el BME280 y los envía **cifrados** por LoRa |

## Firmware (PlatformIO)

```bash
cd Bridge
cp include/secrets.h.example include/secrets.h   # WiFi, broker MQTT y clave LoRa
python3 -c "import os;print(', '.join(f'0x{b:02x}' for b in os.urandom(16)))"
#   -> pega el resultado en LORA_KEY de secrets.h
pio run -e garage -t upload     # placa del garaje
pio run -e home   -t upload     # placa de casa
pio device monitor              # 115200 baudios
```

Los dos nodos tienen que compilarse con la misma frecuencia, los mismos `LINK_*` (`include/config.h`)
y **la misma `LORA_KEY`** (`include/secrets.h`). Con la clave de ejemplo, el OLED muestra «CLAVE EJEMPLO!».
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

**Latencia:** depende de `LINK_SF`. Con SF12, un aviso de movimiento tarda unos 1.8 s y, por el
ciclo de trabajo, puede haber unos 15 s entre mensajes seguidos. Con SF9 baja a ~0.25 s y ~2.5 s.
Por eso, **cuando tengas los resultados del proyecto [Testing](../Testing), baja `LINK_SF`** al SF más bajo con ~100 % de
entrega y margen de SNR, y graba de nuevo las **dos** placas.

## Seguridad

### Cifrado y autenticación (AES-128-CCM)

El mismo esquema que usa LoRaWAN. Lo implementa la mbedtls que ya trae el ESP32: no hace falta ninguna librería extra.

```
Datos:  [cabecera 3 B] [nonce aleatorio 8 B] [datos cifrados 13 B] [MIC 8 B]   = 32 B
ACK:    [cabecera 3 B] [MIC 8 B, calculado sobre el nonce del mensaje]          = 11 B
```

- **Nadie sin la clave puede leer** el estado de los sensores.
- **Nadie sin la clave puede inventar ni modificar** mensajes. Si cambia un solo bit, la firma (MIC) no cuadra
  y casa descarta la trama: no la confirma ni la publica, y suma 1 a **Mensajes rechazados**.
- **Los ACK también van firmados** y ligados al mensaje concreto que confirman.
  Nadie puede hacer creer al garaje que su aviso llegó cuando no llegó.
- El nonce sale del generador aleatorio por hardware del ESP32, así que no se repite.
- **Limitación aceptada:** no hay protección contra *replay*. Alguien podría grabar un mensaje legítimo y
  volver a emitirlo más tarde. No podría leerlo ni modificarlo, pero sí repetir un estado antiguo.

Coste en tiempo de transmisión, por mensaje:

| SF | Sin cifrar | Cifrado | ACK |
|----|-----------|---------|-----|
| 7  | 51 ms   | 72 ms   | 41 ms |
| 9  | 165 ms  | 247 ms  | 144 ms |
| 12 | 1.3 s   | 1.8 s   | 1.2 s |

Si sospechas que alguien conoce la clave (por ejemplo, porque te han robado la placa del garaje),
genera una nueva y graba las dos placas.

### Detección de inhibición (jamming)

Un inhibidor de radio no se puede evitar, pero sí detectar:

- **Garaje no disponible:** si casa deja de oír al garaje durante 3 latidos (~3.5 min),
  todas sus entidades pasan a `unavailable`.
- **Ruido del canal** (`sensor.puente_lora_noise`): casa mide el RSSI del canal cada 500 ms y publica el
  **mínimo de cada minuto**. Los mensajes normales son ráfagas y no lo suben; un inhibidor, que emite sin parar, sí.
  En un sitio tranquilo suele estar en −120 / −125 dBm. Observa tu valor normal unos días y pon el umbral
  unos 15 dB por encima.
- **Mensajes rechazados** (`sensor.puente_lora_rejected`): si sube, alguien está emitiendo tramas
  con un formato parecido pero sin la clave correcta.

Si el garaje cae y a la vez el ruido sube, es casi seguro una inhibición. Si cae sin que suba el ruido,
lo normal es una avería o un corte de luz en el garaje.

Automatización de ejemplo para Home Assistant (*Ajustes → Automatizaciones → editar en YAML*).
Cambia `notify.mobile_app_mi_movil` por tu servicio de notificación:

```yaml
alias: "Garaje: alerta de enlace LoRa"
mode: queued
triggers:
  - trigger: state
    entity_id: sensor.garaje_rssi
    to: "unavailable"
    for: "00:01:00"
    id: caido
  - trigger: numeric_state
    entity_id: sensor.puente_lora_noise
    above: -105          # ajusta: tu ruido normal + ~15 dB
    for: "00:02:00"
    id: ruido
  - trigger: state
    entity_id: sensor.puente_lora_rejected
    id: rechazado
conditions:
  - condition: template
    value_template: >
      {{ trigger.id != 'rechazado' or
         (trigger.to_state.state | int(0)) > (trigger.from_state.state | int(0)) }}
actions:
  - action: notify.mobile_app_mi_movil
    data:
      title: "⚠️ Garaje (LoRa)"
      message: >
        {% if trigger.id == 'caido' %}
          Sin noticias del garaje desde hace más de 4 minutos.
          Ruido del canal: {{ states('sensor.puente_lora_noise') }} dBm
          {{ '→ POSIBLE INHIBIDOR' if states('sensor.puente_lora_noise') | float(-200) > -105 else '' }}
        {% elif trigger.id == 'ruido' %}
          Ruido alto en el canal LoRa: {{ states('sensor.puente_lora_noise') }} dBm. Posible inhibidor.
        {% else %}
          Mensaje LoRa con firma no válida (total {{ trigger.to_state.state }}). Alguien intenta suplantar al garaje.
        {% endif %}
```

## Home Assistant

1. Integración **MQTT** de HA apuntando al mismo broker que usa el nodo de casa.
2. Graba las dos placas. Con el primer mensaje del garaje aparecen dos dispositivos:
   - **Garaje (LoRa)**: Movimiento, Puerta, Temperatura, Humedad, Presión y, como diagnóstico,
     Activaciones PIR, RSSI y SNR de los dos sentidos y Tiempo encendido.
   - **Puente LoRa (casa)**: conectividad del nodo de casa, **Ruido del canal** y **Mensajes rechazados**.
3. Las entidades se llaman `binary_sensor.garaje_motion`, `binary_sensor.garaje_door`,
   `sensor.garaje_temperature`, etc. Úsalas en automatizaciones como cualquier otra.

El prefijo de descubrimiento es `homeassistant`; si usas otro, cambia `HA_PREFIX` con `-DHA_PREFIX=...`.

## MQTT

| Topic | Contenido |
|-------|-----------|
| `lora32/status` | `online` / `offline` del nodo de casa (retenido, LWT) |
| `lora32/garage/state` | `{"motion":false,"door_open":false,"temperature":14.2,"humidity":61,"pressure":1013.4,"motion_events":12,"rssi":-112,"snr":-3.5,"rssi_up":-110,"uptime_min":340}` (retenido) |
| `lora32/garage/availability` | `online` / `offline` (retenido) |
| `lora32/home/state` | `{"noise_dbm":-123,"rejected":0}` (cada minuto, y al instante si se rechaza una trama) |
| `homeassistant/.../config` | MQTT Discovery (retenido) |
