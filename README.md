# LILYGO LoRa32: prueba punto a punto de SF

Dos placas LILYGO LoRa32 (SX1276, 868 MHz) para medir el enlace **casa ↔ garaje**
con distintos *spreading factors* (SF7…SF12).

| Nodo | Dónde | Qué hace |
|------|-------|----------|
| `home`   | Casa: WiFi + MQTT + Node-RED | Lanza los barridos, mide y publica los resultados |
| `garage` | Garaje: solo alimentación   | Responde a cada ping con el RSSI/SNR con el que lo oyó |

En cada ping se miden **los dos sentidos**:

- **↑ casa → garaje**: RSSI/SNR medidos en el garaje (viajan dentro del pong) y nº de pings que oyó.
- **↓ garaje → casa**: RSSI/SNR del pong medidos en casa.

## Cómo funciona

```
 canal de control (SF12, siempre)         parámetros a probar (SFx)          control
 ───────────────────────────────          ─────────────────────────          ──────────
 casa  ── CFG(SFx, N, potencia) ──►       ── PING 0 ──►  ◄── PONG 0 ──       ── REP_REQ ──►
       ◄────────── CFG_ACK ───────        ── PING 1 ──►  ◄── PONG 1 ──       ◄── REPORT ───
                                          ...  N veces                       (pings oídos)
```

1. Casa y garaje escuchan siempre en un **canal de control con SF12** (el más robusto),
   así el aviso de cambio de parámetros llega aunque el SF que se va a probar no llegue.
2. Ambos cambian al SF a probar, se intercambian N ping/pong y vuelven al canal de control.
3. Casa pide al garaje el informe de cuántos pings recibió, para separar las pérdidas de subida y de bajada.
4. Si algo se pierde, el garaje vuelve solo al canal de control cuando se le acaba la ventana de la prueba: nunca se queda colgado en otro SF.

### Frecuencia y ciclo de trabajo

Por defecto se usa **869.525 MHz** (sub-banda g3 de la UE: **10 %** de ciclo de trabajo).
Los dos nodos respetan ese ciclo: el intervalo entre pings se calcula solo a partir del tiempo en el aire.
Con 20 pings por SF:

| SF | ToA ping | intervalo | ~duración |
|----|---------|-----------|-----------|
| 7  | 31 ms   | 0.6 s  | 35 s |
| 8  | 62 ms   | 0.9 s  | 42 s |
| 9  | 124 ms  | 1.6 s  | 56 s |
| 10 | 248 ms  | 3.1 s  | 85 s |
| 11 | 496 ms  | 6.0 s  | 2.4 min |
| 12 | 827 ms  | 10.1 s | 3.8 min |

**Un barrido SF7-12 completo tarda unos 10 minutos.** Si cambias a 868.1 MHz (límite del 1 %)
tienes que poner `DUTY_CYCLE 0.01f` en `include/config.h`, y entonces todo tarda 10 veces más.

## Hardware

Por defecto se compila para la **LoRa32 V2.1 (T3 V1.6.x)**. Para la V1.0 usa los entornos `home_v1` / `garage_v1`.
Los pines están en `include/config.h`. **No enciendas nunca las placas sin antena.**

## Firmware (PlatformIO)

```bash
cp include/secrets.h.example include/secrets.h   # WiFi y broker MQTT
pio run -e garage -t upload     # placa del garaje
pio run -e home   -t upload     # placa de casa
pio device monitor              # 115200 baudios
```

Los dos nodos tienen que compilarse con la misma frecuencia, *sync word* y canal de control (`include/config.h`).

El OLED de cada placa muestra el SF actual, los contadores y el último RSSI/SNR,
así que en el garaje puedes ver si está recibiendo.

### Lanzar un barrido

- **Node-RED**: con el formulario del dashboard.
- **Botón BOOT** de la placa de casa: barrido por defecto (SF7-12, 20 pings, 14 dBm). Si lo pulsas otra vez, se para.
- **Monitor serie** de la placa de casa: `start`, `stop` o un JSON como los del apartado MQTT.

## MQTT

| Topic | Sentido | Contenido |
|-------|---------|-----------|
| `lora32/cmd`     | → casa | `{"action":"start","sf":[7,9,12],"count":20,"power":14,"bw":125,"cr":5,"loop":false}` · `{"action":"stop"}` |
| `lora32/status`  | casa → | `online` / `offline` (retenido, LWT) |
| `lora32/sweep`   | casa → | `{"event":"start"/"end"/"stopped", ...}` |
| `lora32/ping`    | casa → | por ping: `sf, seq, ok, rssi_up, snr_up, rssi_dn, snr_dn, ferr_hz, rtt_ms` |
| `lora32/summary` | casa → | por SF: `sent, garage_rx, pongs, pdr_up, pdr_dn, pdr_rt`, medias RSSI/SNR, `toa_ms`… |

- `pdr_up` = pings oídos en el garaje / enviados (casa→garaje)
- `pdr_dn` = pongs oídos en casa / pings oídos en el garaje (garaje→casa)
- `pdr_rt` = ida y vuelta completa

## Node-RED

1. Instala desde *Manage palette* **`@flowfuse/node-red-dashboard`** (Dashboard 2).
2. Importa `nodered/lora32-sf-test.json`.
3. Ajusta el nodo **Broker local** (host/puerto/usuario de tu broker).
   ¿No tienes broker? Instala `node-red-contrib-aedes` e importa también `nodered/broker-aedes.json`.
4. Abre `http://<node-red>:1880/dashboard/sf`.

El dashboard incluye:

- un formulario para lanzar o parar barridos;
- el % de entrega por SF en cada sentido;
- una tabla resumen;
- el RSSI/SNR de cada ping;
- el RSSI/SNR medio por SF.

Además, los resultados se guardan en `lora32_summary.csv` y `lora32_pings.csv`, en el directorio de trabajo de Node-RED.

## Interpretar resultados

- Límite de demodulación del SX1276 (SNR): SF7 ≈ −7.5 dB, SF8 −10, SF9 −12.5, SF10 −15, SF11 −17.5, SF12 −20 dB.
  Si el SNR medio está cerca de ese límite, el enlace va justo con ese SF.
- Cada SF más alto aporta unos 2.5 dB de margen, pero duplica el tiempo en el aire.
- El SF óptimo es el más bajo con ~100 % de entrega y unos 5-10 dB de margen de SNR.
