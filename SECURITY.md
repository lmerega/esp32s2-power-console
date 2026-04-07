# Security Policy

## Ambito

Questo firmware controlla da remoto il pulsante power di un PC tramite ESP32-S2. Di conseguenza:

- segreti e credenziali non devono essere pubblicati nel repository
- i percorsi di recovery locale devono restare funzionanti anche se Telegram non e disponibile
- le modifiche che toccano OTA, boot, watchdog o comandi sensibili meritano una review piu attenta

## Segnalazione vulnerabilita

Per vulnerabilita reali o sospette:

- non aprire issue pubbliche con credenziali, token o dettagli sfruttabili
- descrivi il problema in modo minimo e riproducibile
- includi versione firmware, board, partizione usata e sintomo osservato

Se il problema coinvolge credenziali gia esposte:

- ruota subito token Telegram e password WiFi
- non allegare i valori reali nei report

## Cosa consideriamo sensibile

- `telegram_secrets.h`
- credenziali WiFi
- endpoint o configurazioni che permettano reboot, OTA o azioni sui GPIO
- dettagli che possano facilitare accesso remoto indesiderato al device

## Hardening atteso

Le patch di sicurezza dovrebbero preservare almeno:

- compatibilita con `ESP32-S2`
- boot non bloccante senza USB CDC
- recovery locale via Web UI e OTA
- diagnostica persistente sufficiente per capire reboot e freeze
