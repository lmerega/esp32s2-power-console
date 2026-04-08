# ESP32 Power Console

Firmware per `LOLIN S2 Mini` usato come controllo remoto del tasto power del PC tramite optoisolatore.

Versione firmware principale corrente:

- `1.5.18`

Ultima build verificata localmente:

- data: `2026-04-08`
- main bin: `1,146,000 B`
- sketch usage: `1,145,851 B / 1,507,328 B` (`76%`)
- RAM globale: `68,868 B / 327,680 B` (`21%`)
- build locale completata con successo: `si`

## Funzioni principali

- Web dashboard locale con istruzioni integrate
- Fallback `WiFiManager` in AP mode
- Comando `pulse` per accensione PC
- Comando `force shutdown` a pressione lunga
- Modalita `TEST ON/OFF` per diagnostica pin
- Timeout automatico per `TEST ON`
- Report HTTP leggero verso Node-RED al boot e alla riconnessione WiFi
- Bridge Telegram esterno via Node-RED per `/ip`, `/status`, `/pulse` e `/force`
- Keepalive Node-RED via `GET /api/status` ogni `10` minuti
- Notifica Telegram automatica da Node-RED su `boot` e `wifi_reconnected`
- Web OTA
- Timing persistenti in `Preferences`
- Buglog persistente in `NVS` con `magic`, `CRC8` e ring buffer
- Heartbeat diagnostico persistente per `heap`, WiFi e stato task
- Scan WiFi asincrono dalla dashboard
- Nessun passaggio automatico in recovery factory per sola perdita WiFi
- Fallback automatico in `factory` dopo `3` boot falliti persistiti in `NVS`
- Azzeramento del contatore boot solo dopo `10` minuti di uptime sano
- Favicon SVG inline con icona power su dashboard e pagina OTA

## Board e build

- Board target: `LOLIN S2 Mini`
- FQBN: `esp32:esp32:lolin_s2_mini`
- Sketch principale: [ESP32S2_WiFiManager_WebServer.ino](ESP32S2_WiFiManager_WebServer.ino)
- Build wrapper: [build_bin.bat](build_bin.bat)
- Build script: [build_bin.ps1](build_bin.ps1)

Tooling essenziale mantenuto nel progetto:

- `build_bin.ps1` / `build_bin.bat`
- `build_recovery_bin.ps1` / `build_recovery_bin.bat`
- `flash.ps1` / `flash.bat`
- `flash_full_serial.ps1` / `flash_full_serial.bat`
- `esp32_layout_common.ps1`

Uso rapido:

1. Esegui `build_bin.bat`
2. Oppure esegui `build_bin.ps1`
3. Se vuoi incrementare automaticamente la patch version usa `build_bin.bat bump`
4. Se `arduino-cli` non e nel path standard usa `build_bin.ps1 -ArduinoCliPath "C:\\percorso\\arduino-cli.exe"`

Note build:

- Il bump versione non e piu obbligatorio a ogni compilazione
- Il wrapper `.bat` accetta anche l'alias rapido `bump`
- Lo script continua a pulire i `.bin` precedenti e a copiare gli output nella cartella dello sketch
- Gli script di test PowerShell non fanno piu parte del workspace operativo
- La build validata piu recente del main firmware pesa `1,146,000 B`

## Repository e GitHub

Regole consigliate per il repository:

- versionare sorgenti, script, partizioni, documentazione e firmware recovery
- non versionare artefatti di build, log temporanei, cartelle `build/` e segreti locali
- non pubblicare `telegram_secrets.h`
- non pubblicare `local_services.h`

Setup locale servizi:

1. copia `local_services.example.h` in `local_services.h`
2. inserisci `NODERED_REPORT_URL_VALUE`, ad esempio `http://192.168.90.60:1880/esp32/presence`
3. compila normalmente con `build_bin.bat`

Setup locale opzionale bot Telegram:

1. se vuoi riusare il token del bot anche fuori da Node-RED, copia `telegram_secrets.example.h` in `telegram_secrets.h`
2. inserisci `TELEGRAM_BOT_TOKEN_VALUE` e `TELEGRAM_CHAT_ID_VALUE`

Per GitHub i file reali `telegram_secrets.h` e `local_services.h` vanno ignorati; i template versionati restano `telegram_secrets.example.h` e `local_services.example.h`.

## Mappa del progetto

### Root del progetto

- `ESP32S2_WiFiManager_WebServer.ino`
  Firmware principale. Gestisce GPIO power, dashboard web, WiFi, fallback AP, OTA e il report presence verso Node-RED.

- `README.md`
  Documento operativo principale. Spiega uso, build, flash, API e struttura del progetto.

- `CHANGELOG.md`
  Storico delle modifiche rilevanti al firmware e al tooling.

- `partitions_factory.csv`
  Tabella partizioni della flash da 4 MB. Definisce `factory`, `ota_0`, `ota_1`, `coredump`, `nvs`, `otadata`.

- `partitions.csv`
  Copia compatibile della tabella partizioni usata come fallback per ambienti `arduino-cli` che cercano il nome standard `partitions.csv`.

- `build_bin.ps1`
  Script principale per compilare il firmware normale e generare i `.bin` aggiornati.

- `build_bin.bat`
  Wrapper rapido Windows per lanciare `build_bin.ps1`.

- `build_recovery_bin.ps1`
  Script per compilare il firmware recovery/factory.

- `build_recovery_bin.bat`
  Wrapper rapido Windows per lanciare `build_recovery_bin.ps1`.

- `flash.ps1`
  Script per aggiornamento del firmware principale via rete/OTA HTTP.

- `flash.bat`
  Wrapper rapido Windows per lanciare `flash.ps1`.

- `flash_full_serial.ps1`
  Script per flash seriale completo del dispositivo. Da usare quando serve scrivere in modo robusto bootloader, partizioni e firmware.

- `flash_full_serial.bat`
  Wrapper rapido Windows per lanciare `flash_full_serial.ps1`.

- `esp32_layout_common.ps1`
  Helper condiviso dagli script PowerShell. Contiene offset, validazioni e logica comune del layout ESP32.

- `telegram_secrets.example.h`
  Template locale per configurare le credenziali Telegram senza versionare i segreti reali.

- `local_services.example.h`
  Template locale per configurare l'endpoint Node-RED che riceve il report IP/presenza della board.

- `node-red/esp32-power-console-flow.json`
  Flow Node-RED importabile per il bridge Telegram, la cache IP e il keepalive HTTP verso la board.

- `node-red/README.md`
  Guida rapida all'import del flow Node-RED e alla configurazione del bridge esterno.

### Recovery

- `ESP32S2_Recovery_OTA/ESP32S2_Recovery_OTA.ino`
  Firmware recovery/factory. Serve come ultima difesa per riportare online la scheda e caricare un nuovo firmware.

- `ESP32S2_Recovery_OTA/partitions_factory.csv`
  Copia locale della tabella partizioni usata nel sottoprogetto recovery.

- `ESP32S2_Recovery_OTA/partitions.csv`
  Copia compatibile della tabella partizioni per la build recovery su ambienti che richiedono il nome standard `partitions.csv`.

- `ESP32S2_Recovery_OTA/`
  Sottoprogetto recovery/factory completo, versionato insieme al firmware principale.

### File generati localmente ma non versionati

Questi file possono comparire nella cartella di lavoro dopo la build o il flash, ma non fanno parte del repository GitHub:

- `ESP32S2_WiFiManager_WebServer.ino.bin`
- `ESP32S2_WiFiManager_WebServer.ino.bootloader.bin`
- `ESP32S2_WiFiManager_WebServer.ino.partitions.bin`
- `ESP32S2_WiFiManager_WebServer.ino.merged.bin`
- `ESP32S2_Recovery_OTA/ESP32S2_Recovery_OTA.ino.bin`
- `ESP32S2_Recovery_OTA/ESP32S2_Recovery_OTA.ino.bootloader.bin`
- `ESP32S2_Recovery_OTA/ESP32S2_Recovery_OTA.ino.partitions.bin`
- `ESP32S2_Recovery_OTA/ESP32S2_Recovery_OTA.ino.merged.bin`
- `boot_app0.bin`
- `build/`
- `ESP32S2_Recovery_OTA/build/`
- `build_last_compile*.log`
- `telegram_secrets.h`
- `local_services.h`
- `hourly_observation_*.txt`

Sono output locali o file sensibili esclusi tramite `.gitignore`.

### Cosa usare davvero nella pratica

- Per compilare il firmware principale: `build_bin.ps1` oppure `build_bin.bat`
- Per compilare il recovery: `build_recovery_bin.ps1` oppure `build_recovery_bin.bat`
- Per aggiornare via rete: `flash.ps1` oppure `flash.bat`
- Per riflashare via seriale: `flash_full_serial.ps1` oppure `flash_full_serial.bat`
- Per OTA dalla web UI: usare solo `ESP32S2_WiFiManager_WebServer.ino.bin`

## Configurazione persistente

Salvata nel namespace `Preferences`:

- namespace `pcpower`
- `pulse_ms`
- `force_ms`
- `wifi_ssid`
- `wifi_pass`

Salvata nel namespace `buglog`:

- eventi persistenti runtime
- heartbeat diagnostico periodico
- entry validate con `magic` + `CRC8`

## Web UI

Pagine disponibili:

- `/`
- `/update`

Sezioni diagnostiche:

- `Log RAM` per tracing volatile di sessione
- `Buglog Persistente` per eventi che devono sopravvivere a reboot e power cycle
- legenda inline per `severity`, codici e campi heartbeat (`fh`, `mb`, `wf`, `la`, `ta`)
- diagnostica runtime della board senza dipendere dal bridge Telegram esterno

La dashboard ora mostra:

- stato corrente del device
- chip model e reset reason
- info WiFi e timeout `TEST ON`
- istruzioni rapide d'uso
- log RAM volatili
- buglog persistente con pulsanti `Aggiorna`, `Copy`, `Pulisci`
- legenda buglog sotto la finestra di log
- legenda API aggiornata

Azioni delicate con conferma custom:

- `TEST ON`
- `FORCE SHUTDOWN`
- `REBOOT ESP`
- `WEB OTA`
- `RESET WIFI`

## Istruzioni di utilizzo

### Accensione PC

- usa `PULSE`
- il firmware invia un impulso breve configurabile

### Spegnimento forzato

- usa `FORCE SHUTDOWN`
- il firmware invia una pressione lunga configurabile
- usalo solo se il sistema operativo non risponde

### Test del pin power

- usa `TEST ON` solo per diagnostica
- il pin resta alto finche non fai `TEST OFF` oppure finche scade il timeout automatico
- il timeout automatico predefinito e `30` minuti

### Configurazione WiFi

- puoi salvare SSID e password dalla dashboard
- la configurazione WiFi ora usa `POST /api/config/wifi`
- il reboot dopo il salvataggio resta una scelta esplicita dell'utente
- se le credenziali sono errate il device puo tornare in AP mode

### Reset WiFi

- `RESET WIFI` cancella le credenziali persistenti
- dopo il reset il device si riavvia e torna in modalita AP

### OTA

1. compila il `.bin`
2. apri `http://IP_ESP/update`
3. carica `ESP32S2_WiFiManager_WebServer.ino.bin`

Per OTA va caricato solo il firmware principale:

- `ESP32S2_WiFiManager_WebServer.ino.bin`

Non usare via OTA:

- `bootloader.bin`
- `partitions.bin`

## API HTTP

- `GET /api/status`
- `GET /api/logs?limit=30`
- `GET /api/logs/clear`
- `GET /api/buglog`
- `GET /api/buglog/clear`
- `GET /api/boot-history?limit=12`
- `GET /api/pc/pulse`
- `GET /api/pc/forceshutdown`
- `GET /api/pc/test/on`
- `GET /api/pc/test/off`
- `GET /api/config/timings?pulse_ms=500&force_ms=3000`
- `GET /api/wifi/scan`
- `POST /api/config/wifi`
- `GET /api/reboot`
- `GET /api/boot-recovery`
- `POST /api/update`

Nota:

- `GET /api/config/wifi` ora risponde con `405 use_post`

Buglog persistente:

- `GET /api/buglog` restituisce il dump testuale del ring buffer persistente
- `GET /api/buglog/clear` azzera il buffer persistente
- `code=1500` heartbeat normale ogni `15` minuti
- `code=1501` heartbeat anomalo ogni `2` minuti se rileva condizioni sospette
- il refresh di `GET /api/status` non genera piu warning `1603` nel buglog persistente
- `fh` = free heap, `mb` = max alloc block, `wf` = WiFi `0/1`, `la` = eta heartbeat loop, `ta` = eta heartbeat Telegram

Esempio body per la configurazione WiFi:

- `ssid=NomeRete&password=PasswordRete`

## Bridge Telegram

Nella versione attuale Telegram non gira piu sull'ESP32-S2. Il canale Telegram e' esterno e viene gestito da Node-RED nella stessa LAN.

Il bridge fa questo:

- riceve dal firmware il report presence via `POST /esp32/presence`
- salva l'ultimo IP noto della board
- interroga la board con `GET /api/status` ogni `10` minuti
- se Node-RED e' giu, il firmware ritenta il report presence con backoff progressivo fino a `120` secondi
- risponde ai comandi Telegram `/ip`, `/status`, `/pulse`, `/force`
- invia una notifica Telegram automatica su `boot` e `wifi_reconnected`

Vantaggi pratici:

- niente polling Telegram o TLS pesante sulla board
- memoria dell'ESP32-S2 stabile anche su run lunghi
- bridge e bot gestiti su un host piu robusto
- fallback locale sempre disponibile via Web UI, AP fallback e OTA

File utili:

- `node-red/esp32-power-console-flow.json`
- `node-red/README.md`
- `local_services.example.h`

## Note hardware

Uso previsto:

- lato transistor optoisolatore in parallelo ai pin `PWR_SW` della motherboard
- il firmware non deve mai lasciare il pin alto prima di reboot o reset

Protezione implementata:

- prima di `reboot`
- prima di `reset wifi`
- prima del reboot post-OTA
- a timeout del `TEST ON`

il firmware forza il rilascio dell'uscita power.
