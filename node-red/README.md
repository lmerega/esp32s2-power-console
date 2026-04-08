# Node-RED Bridge

Architettura consigliata:

- ESP32-S2 senza Telegram on-board
- report HTTP leggero dell'IP al boot e alla riconnessione WiFi
- Telegram gestito interamente da Node-RED
- keepalive leggero da Node-RED con `GET /api/status` ogni 10 minuti
- notifica Telegram automatica sugli eventi `boot` e `wifi_reconnected`

Perche' non MQTT, per ora:

- l'ESP ha gia' un'API HTTP locale pronta
- il report presence e' un singolo POST molto leggero
- il keepalive ogni 10 minuti e' trascurabile
- evitiamo un broker in piu' finche' non serve davvero

## Requisiti Node-RED

Installa il pacchetto:

- `node-red-contrib-telegrambot`

## Variabili del tab

Il flow usa le env del tab `ESP32 Power Console`:

- `ESP32_FALLBACK_URL`
- `TELEGRAM_ALLOWED_CHAT_ID`
- `TELEGRAM_NOTIFY_CHAT_ID`

Se importi il flow da zero, apri il tab e compila questi valori dal pannello `Edit flow`.

## Variabili ambiente Docker

Facoltative. Ti servono solo se vuoi passare il token al container o averlo a portata di mano durante il primo setup del bot:

```yaml
environment:
  - TZ=Europe/Rome
  - TELEGRAM_BOT_TOKEN=123456:ABCDEF...
```

Note:

- `ESP32_FALLBACK_URL` e' il piano B, utile anche con DHCP reservation.
- quando il firmware invia il report di presenza a Node-RED, il flow usera' l'IP ricevuto come sorgente preferita.
- `TELEGRAM_NOTIFY_CHAT_ID`, se valorizzato, riceve automaticamente la notifica boot/reconnect.

## Endpoint Node-RED

Il flow espone:

- `POST /esp32/presence`

Il firmware puo' inviare qui un JSON simile a:

```json
{
  "reason": "boot",
  "ip": "192.168.90.3",
  "host": "esp32s2",
  "fw": "1.5.17",
  "fw_full": "1.5.17 (Apr 7 2026 23:55:00)",
  "boot_id": "DFA02135",
  "running_partition": "ota_0",
  "wifi_ssid": "lmewififlint",
  "rssi": -48,
  "rome_time": "2026-04-08 09:22:50.022 CEST"
}
```

## Comandi Telegram

Il flow risponde a:

- `/ip`
- `/status`
- `/pulse`
- `/force`
- `/help`

Comportamento:

- `/ip` usa l'ultimo report ricevuto dalla board
- `/status` usa l'ultimo keepalive salvato da Node-RED
- `/pulse` e `/force` chiamano in tempo reale l'API della board
- `boot` e `wifi_reconnected` inviano un Telegram automatico alla chat configurata

## Persistenza

Il flow salva due file nel volume `/data` di Node-RED:

- `/data/esp32-power-console-last.json`
- `/data/esp32-power-console-status.json`

Quindi, anche se riavvii il container, l'ultimo IP noto resta disponibile.

## Import

Importa il file:

- `node-red/esp32-power-console-flow.json`

Poi:

1. configura il bot Telegram nel nodo `Telegram RX` o `Telegram TX`
2. imposta `TELEGRAM_ALLOWED_CHAT_ID`
3. imposta `TELEGRAM_NOTIFY_CHAT_ID`
4. imposta `ESP32_FALLBACK_URL`
5. Deploy

## Firmware

Per attivare il report IP dal firmware, crea un file locale non versionato:

- `local_services.h`

partendo da:

- `local_services.example.h`

e imposta, ad esempio:

```cpp
#pragma once
#define NODERED_REPORT_URL_VALUE "http://192.168.90.60:1880/esp32/presence"
```

Poi ricompila e riflasha il firmware.
