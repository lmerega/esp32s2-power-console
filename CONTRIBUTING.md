# Contributing

Grazie per l'interesse nel progetto.

## Prima di iniziare

- apri una issue per bug reali, regressioni o richieste di miglioramento
- per modifiche piccole puoi aprire direttamente una pull request
- evita di includere segreti reali, dump di rete sensibili o credenziali WiFi

## Ambiente consigliato

- board: `LOLIN S2 Mini`
- core ESP32 Arduino: `3.3.7`
- Arduino IDE `2.x` oppure `arduino-cli`
- Windows e PowerShell sono il percorso operativo principale del repository

Librerie minime usate dal firmware:

- `WiFiManager >= 2.0.17`
- `ArduinoJson >= 7.4.2`

## Convenzioni pratiche

- non committare `telegram_secrets.h`
- mantieni aggiornato `telegram_secrets.example.h` se cambia il contratto di configurazione
- non committare binari, log di build o cartelle `build/`
- aggiorna `README.md` se aggiungi endpoint, script o flussi operativi
- aggiorna `CHANGELOG.md` per modifiche rilevanti lato firmware, diagnostica o tooling

## Pull request

Una PR buona per questo progetto dovrebbe includere:

- contesto del problema o motivazione della modifica
- hardware o scenario coperto
- impatto su OTA, recovery, WiFi o Telegram
- test eseguiti davvero

Esempi di test utili:

- build completa del firmware principale
- build del recovery
- verifica Web UI
- verifica OTA
- verifica di almeno un caso Telegram se il codice lo tocca
- verifica dei log persistenti se tocchi watchdog, boot o diagnostica

## Scope

Il progetto e orientato a:

- affidabilita su `ESP32-S2`
- recupero locale via Web UI, AP fallback e recovery
- diagnostica persistente utile in campo

Evita PR che aggiungono complessita non necessaria o che trasformano Telegram in canale privilegiato per operazioni critiche.
