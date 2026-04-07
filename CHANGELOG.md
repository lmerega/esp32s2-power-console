# Changelog

## 2026-04-07

- Bump firmware principale fino a `1.5.12`
- Fix regressione Telegram: recovery automatica del `telegramTask` se heartbeat/task si bloccano o il task sparisce
- Nuovo endpoint operativo `GET /api/telegram/recover`
- `GET /api/telegram/recover?reset_offset=1` forza anche il reset dell'offset persistito di `getUpdates`
- `/api/telegram/health` ora espone anche `task_running`, `task_restart_count`, `last_update_id` e heartbeat task
- Aggiunti codici buglog Telegram `1310`, `1311`, `1312` per recovery task e ingresso in failsafe
- Aggiunto `buglog` persistente in namespace `NVS` dedicato con ring buffer, `magic` e `CRC8`
- Nuovi endpoint `GET /api/buglog` e `GET /api/buglog/clear`
- Aggiunto pannello `Buglog Persistente` nella Web UI con pulsanti `Aggiorna`, `Copy`, `Pulisci`
- Aggiunta legenda inline del buglog nella dashboard con significato di severity, codici e campi heartbeat, ora posizionata sotto la finestra log
- Inserito heartbeat diagnostico persistente:
  - `code=1500` in condizioni normali ogni `15` minuti
  - `code=1501` in condizioni anomale ogni `2` minuti
- Heartbeat runtime ora salva `free heap`, `max alloc block`, stato WiFi e eta heartbeat di loop/task Telegram
- Resa affidabile la risposta HTTP di `/api/buglog` inviando il dump come body completo
- Logging seriale e `Update.printError()` resi safe su ESP32-S2 con USB CDC non connessa
- Stato Telegram sensibile spostato su buffer fissi protetti invece di `String` condivise tra task
- `handleResetWiFi()` ora verifica correttamente il lock su `Preferences`
- Reconnect WiFi reso rollover-safe rispetto a `millis()`
- Scan WiFi web reso asincrono con timeout per evitare blocchi del loop
- Aumentata la riserva JSON di `/api/status` per ridurre riallocazioni heap
- Aumentato stack di `safetyTask` per accomodare logging persistente e path watchdog
- README aggiornato con API, recovery Telegram, buglog, build `1.5.12` e nuove note operative

## 2026-03-26

- Bump firmware principale a `1.5.6`
- `build_bin.bat` ora accetta anche l'alias rapido `bump` e lo converte in `-BumpVersion`
- `build_bin.ps1` ora salva il log in `build_last_compile.log` e ripristina `FW_VERSION` se il bump fallisce
- Build main e recovery rese indipendenti dal fallback del core ESP32 aggiungendo il mirror locale `partitions.csv`
- Telegram semplificato a canale secondario read-only per discovery IP e stato
- Supporto esplicito a `/start`, con risposta di bootstrap e guida comandi minima
- Rimossi i comandi Telegram che agivano su GPIO, reboot, OTA e recovery
- Poll Telegram ridotto a long-poll lento solo su `message`
- Rimosso il coupling tra `telegramTask` e watchdog applicativo
- Eliminato il boot automatico in factory su sola perdita WiFi
- Ridotto `WEB_LOG_CAPACITY` da `300` a `100`
- Aggiunta favicon SVG inline con icona power per dashboard e pagina OTA
- Ultima build main verificata: `1,248,016 B`
- Flash seriale finale completato con successo sull'hardware target
- Aggiornati README e tooling operativo per il nuovo modello Telegram
- Rimossi dal workspace operativo gli script PowerShell di test e il backup locale non necessario

## 2026-03-24

- Hardening completo flusso Telegram contro freeze su invio/ricezione
- `sendTelegramMessageEx()` ora ha timeout brevi e budget totale hard (`TELEGRAM_SEND_TOTAL_BUDGET_MS`)
- Ridotti i timeout TCP/TLS Telegram per evitare blocchi lunghi del task
- Gestione esplicita `HTTP 409` su `getUpdates` con auto-recovery (`deleteWebhook`) pianificato
- Nuovo stato di recovery webhook con scheduler e cooldown anti-loop
- `tgPost`/`tgGet` ora conservano anche il body errore e la `description` Telegram
- Persistenza offset Telegram resa batch (`persistTelegramOffsetIfNeeded`) per evitare write NVS a ogni update
- Nuove metriche Telegram in `/api/status` (poll/send/update/error/recovery)
- Nuovo endpoint operativo `GET /api/telegram/health`
- Aggiunta copertura di regressione locale per il sottosistema Telegram

## 2026-03-21

- Rinominato progetto e sketch da `ESP32C3_...` a `ESP32S2_...` per allinearlo alla board reale `LOLIN S2 Mini`
- Bump firmware a `1.1.0`
- Timeout automatico delle conferme Telegram dopo `60` secondi
- Nonce conferme Telegram generato con `esp_random()`
- Nuovo comando Telegram `/help`
- Notifica Telegram alla riconnessione WiFi
- Timeout automatico per `TEST ON` a `30` minuti
- `deviceStatusMessage()` ora usa il chip model reale invece della stringa hardcoded
- `/api/config/wifi` migrata a `POST`
- `GET /api/config/wifi` ora risponde con `405 use_post`
- JSON stato e scan WiFi con escaping piu robusto
- Dashboard web riscritta con:
  - istruzioni d'uso integrate
  - stato piu ricco
  - legenda API aggiornata
  - conferme modali piu chiare
- Pagina Web OTA aggiornata con istruzioni operative piu esplicite
- `build_bin.bat` ora passa gli argomenti a `build_bin.ps1`
- `build_bin.ps1` ora supporta:
  - `-BumpVersion`
  - `-ArduinoCliPath`
  - build senza bump automatico obbligatorio
- README aggiornato con istruzioni d'uso, API e build
- Ridotta ulteriore duplicazione in helper firmware e UI
- Aggiunti campi extra in `/api/status` come `chip_model`, `reset_reason`, stato WiFi e timeout test

## 2026-03-15

- Telegram riportato a messaggi nuovi, senza `editMessage`
- Conferme Telegram usa-e-getta per:
  - `TEST ON`
  - `Spegni forzato`
  - `Riavvia ESP`
- Domande Telegram di conferma cancellate dopo `Si`, `No` o nuovo comando
- Rimossa duplicazione tra `/espstatus` e `/espnotify`
- Tenuto solo `/espstatus`
- Tastiera Telegram aggiornata con icone piu chiare
- Dashboard web con modal custom per azioni delicate
- Rimosso `confirm()` browser in favore della modal interna

## 2026-03-11

- Fix refresh campi timing durante modifica
- Aggiornamento label pulsanti da valori salvati
- Aggiunte conferme per azioni sensibili nella dashboard
- Safe release dell'uscita power prima di:
  - reboot
  - reset wifi
  - reboot post-OTA

## 2026-03-10

- Favicon custom inline
- `build_bin.bat` semplificato come wrapper
- Introdotto `build_bin.ps1`
- Bump automatico `FW_VERSION` prima della build
- Stampa di versione, dimensione e timestamp dei `.bin`

## 2026-03-09

- Passaggio effettivo a `LOLIN S2 Mini`
- Web OTA integrato
- Dashboard con `FW`, `Build`, `Slot`
- Telegram con polling dedicato in task separato
- Uptime esteso in API, dashboard e Telegram
- WiFi configurabile via dashboard
- Timing `pulse_ms` e `force_ms` persistenti
- WiFiManager mantenuto come fallback AP
- Token API rimosso completamente per uso LAN-only

## Stato attuale

- Board target: `esp32:esp32:lolin_s2_mini`
- Firmware con UI locale, Telegram, Web OTA e fallback AP
- Progetto pensato per LAN domestica e controllo power switch PC tramite optoisolatore
