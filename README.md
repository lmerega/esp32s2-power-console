# ESP32 Power Console

Firmware per `LOLIN S2 Mini` usato come controllo remoto del tasto power del PC tramite optoisolatore.

Versione firmware principale corrente:

- `1.5.12`

Ultima build verificata localmente:

- data: `2026-04-07`
- main bin: `1,262,176 B`
- sketch usage: `1,262,019 B / 1,507,328 B` (`83%`)
- RAM globale: `69,260 B / 327,680 B` (`21%`)
- build locale completata con successo: `si`

## Funzioni principali

- Web dashboard locale con istruzioni integrate
- Fallback `WiFiManager` in AP mode
- Comando `pulse` per accensione PC
- Comando `force shutdown` a pressione lunga
- Modalita `TEST ON/OFF` per diagnostica pin
- Timeout automatico per `TEST ON`
- Telegram minimale read-only per discovery IP e stato
- Notifica Telegram al boot e alla riconnessione WiFi
- Auto-recovery del task Telegram se resta bloccato o sparisce
- Web OTA
- Timing persistenti in `Preferences`
- Buglog persistente in `NVS` con `magic`, `CRC8` e ring buffer
- Heartbeat diagnostico persistente per `heap`, WiFi e stato task
- Scan WiFi asincrono dalla dashboard
- Nessun passaggio automatico in recovery factory per sola perdita WiFi
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
- La build validata piu recente del main firmware pesa `1,262,176 B`

## Repository e GitHub

Regole consigliate per il repository:

- versionare sorgenti, script, partizioni, documentazione e firmware recovery
- non versionare artefatti di build, log temporanei, cartelle `build/` e segreti locali
- non pubblicare `telegram_secrets.h`

Setup locale segreti Telegram:

1. copia `telegram_secrets.example.h` in `telegram_secrets.h`
2. inserisci `TELEGRAM_BOT_TOKEN_VALUE` e `TELEGRAM_CHAT_ID_VALUE`
3. compila normalmente con `build_bin.bat`

Per GitHub il file reale `telegram_secrets.h` va ignorato; il template versionato resta `telegram_secrets.example.h`.

## Mappa del progetto

### Root del progetto

- `ESP32S2_WiFiManager_WebServer.ino`
  Firmware principale. Gestisce GPIO power, dashboard web, WiFi, fallback AP, OTA e Telegram read-only.

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

- `boot_app0.bin`
  Binario di supporto ESP32 richiesto dal layout flash completo.

### Recovery

- `ESP32S2_Recovery_OTA/ESP32S2_Recovery_OTA.ino`
  Firmware recovery/factory. Serve come ultima difesa per riportare online la scheda e caricare un nuovo firmware.

- `ESP32S2_Recovery_OTA/partitions_factory.csv`
  Copia locale della tabella partizioni usata nel sottoprogetto recovery.

- `ESP32S2_Recovery_OTA/partitions.csv`
  Copia compatibile della tabella partizioni per la build recovery su ambienti che richiedono il nome standard `partitions.csv`.

- `ESP32S2_Recovery_OTA/boot_app0.bin`
  Copia locale del binario `boot_app0` usata nella build recovery.

### Binari e artefatti utili

- `ESP32S2_WiFiManager_WebServer.ino.bin`
  Binario principale da caricare via Web OTA.

- `ESP32S2_WiFiManager_WebServer.ino.bootloader.bin`
  Bootloader della build corrente del firmware principale.

- `ESP32S2_WiFiManager_WebServer.ino.partitions.bin`
  Tabella partizioni compilata del firmware principale.

- `ESP32S2_WiFiManager_WebServer.ino.merged.bin`
  Immagine completa unificata del firmware principale.

- `ESP32S2_Recovery_OTA/ESP32S2_Recovery_OTA.ino.bin`
  Binario recovery pronto all'uso.

- `ESP32S2_Recovery_OTA/ESP32S2_Recovery_OTA.ino.bootloader.bin`
  Bootloader della build recovery.

- `ESP32S2_Recovery_OTA/ESP32S2_Recovery_OTA.ino.partitions.bin`
  Tabella partizioni compilata del recovery.

- `ESP32S2_Recovery_OTA/ESP32S2_Recovery_OTA.ino.merged.bin`
  Immagine completa unificata del recovery.

### Cartelle di supporto

- `build/`
  Artefatti intermedi della compilazione del firmware principale. Utile per debug, map file e output completi, ma non e un punto da modificare a mano.

- `ESP32S2_Recovery_OTA/build/`
  Artefatti intermedi della compilazione recovery.

- `.claude/`
  Metadati locali dell'ambiente di lavoro. Non servono al firmware.

- `ESP32S2_Recovery_OTA/.claude/`
  Metadati locali del sottoprogetto recovery. Non servono al firmware.

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
- `tg_upd_id`

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
- diagnostica Telegram con stato task, heartbeat e recovery

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
- `GET /api/telegram/health`
- `GET /api/telegram/recover`
- `POST /api/update`

Nota:

- `GET /api/config/wifi` ora risponde con `405 use_post`

Health Telegram:

- `GET /api/telegram/health` espone stato polling/send, backoff/failsafe, ultimi errori HTTP, contatori update/comandi, `task_running`, `task_restart_count`, `last_update_id` e heartbeat task
- `GET /api/telegram/recover` forza il riavvio del sottosistema Telegram
- `GET /api/telegram/recover?reset_offset=1` forza recovery Telegram e azzera l'offset persistito di `getUpdates`

Buglog persistente:

- `GET /api/buglog` restituisce il dump testuale del ring buffer persistente
- `GET /api/buglog/clear` azzera il buffer persistente
- `code=1500` heartbeat normale ogni `15` minuti
- `code=1501` heartbeat anomalo ogni `2` minuti se rileva condizioni sospette
- `fh` = free heap, `mb` = max alloc block, `wf` = WiFi `0/1`, `la` = eta heartbeat loop, `ta` = eta heartbeat Telegram

Esempio body per la configurazione WiFi:

- `ssid=NomeRete&password=PasswordRete`

## Telegram

Telegram e stato ridotto a canale secondario read-only:

- notifica IP al boot
- notifica IP alla riconnessione WiFi
- polling lento e minimale per richieste testuali
- recovery automatica locale se il task Telegram smette di fare polling
- nessun comando remoto che agisce su GPIO, reboot, OTA o recovery

Comandi gestiti:

- `/start`
- `/ip`
- `/status`
- `/help`

Testo consigliato per `/start`:

```text
Power Console pronto.

Comandi disponibili:
/ip - mostra IP, hostname e rete WiFi
/status - mostra stato sintetico del device
/help - mostra questo aiuto

Nota:
Telegram e un canale secondario read-only.
Il recupero locale resta via Web UI, AP fallback e OTA.
```

Testo consigliato per `/help`:

```text
Comandi disponibili:
/start - bootstrap iniziale
/ip - IP corrente, hostname e WiFi
/status - stato sintetico del device
/help - elenco comandi

Telegram e solo read-only:
nessun comando remoto per GPIO, reboot, OTA o recovery.
```

Nota operativa:

- Telegram non deve essere considerato un canale vitale
- se Telegram e offline, il firmware principale continua a funzionare
- il recupero locale resta affidato a web UI, AP fallback e OTA/recovery
- `/start` e il comando di bootstrap predefinito da usare per recuperare IP e stato
- se Telegram smette di reagire, controlla prima `/api/telegram/health`
- se il task risulta fermo o gli update restano in coda, usa `/api/telegram/recover`
- usa `/api/telegram/recover?reset_offset=1` solo se vuoi riallineare da zero il polling

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
