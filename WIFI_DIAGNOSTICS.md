# Diagnostica WiFi e scan (firmware 1.6.3)

Il pannello **Buglog Persistente** e `GET /api/buglog` mostrano eventi conservati in NVS anche dopo un riavvio o un'interruzione di alimentazione. Il buffer conserva gli ultimi 48 eventi, condivisi con la diagnostica gia esistente: non e un archivio illimitato.

## Disconnessioni

| Codice | Significato |
| --- | --- |
| 1700 | Disconnessione: motivo numerico Espressif e nome, RSSI, ultimo canale noto, scan attivo |
| 1701 | IP acquisito: indirizzo, durata offline, chiamate reconnect durante l'interruzione, segnale e canale |
| 1702 | IP perso, distinto dalla disassociazione radio |
| 1703 | Riepilogo reconnect: contatore, esito immediato della chiamata, stato WiFi, durata offline, primo/ultimo motivo e RSSI |
| 1704 | Avvio/arresto dell'interfaccia STA |
| 1705 | Inizializzazione della diagnostica oppure eventi scartati per coda piena |

`ok=1` in un record retry indica che `WiFi.reconnect()` ha accettato la richiesta, non che la connessione sia riuscita: l'esito positivo arriva con `1701`. I tentativi automatici interni al core non incrementano il contatore delle chiamate applicative.

I fallimenti ripetuti durante la stessa interruzione vengono riepilogati al massimo ogni 5 minuti per tipo (`1700` e `1703`); il primo evento e immediato. I contatori e l'ultimo motivo continuano ad aggiornarsi in RAM. Il motivo segnala cio che riporta il driver: non prova da solo un guasto dell'AP, dell'alimentatore o del cavo.

## Scansioni

| Codice | Significato |
| --- | --- |
| 1710 | Inizio scan, numero progressivo, stato WiFi, RSSI, heap libero e blocco massimo |
| 1711 | Esito `done`, `failed`, `start_failed` o `timeout`, codice restituito, durata in ms e heap |
| 1712 | Risposta scan: `build` prima di generare il JSON, `ready` quando pronto, `sent` dopo il ritorno dell'invio HTTP |
| 1713 | Richieste scan rifiutate dal rate limit, con riepilogo ogni 5 minuti |

Il marker `1710` viene accodato e scaricato su NVS **prima** delle chiamate al driver di scansione. Anche i checkpoint `1712` vengono scaricati subito. Le scritture sono soggette alla disponibilita di NVS. Un inizio senza esito, seguito da un boot, permette di localizzare la fase dell'interruzione; non prova da solo un blocco del driver. `sent` conferma il ritorno della funzione HTTP, non la ricezione da parte del browser.

Il loop controlla completamento e timeout di 15 secondi anche se il browser smette di interrogare l'API. Se il driver blocca il loop, il timeout del loop non puo intervenire: resta il marker precedente e il watchdog gia presente. Nessuna scansione viene avviata automaticamente per raccogliere questi log.

La dashboard interroga lo scan ogni 4 secondi, rispettando i limiti di 2,5 secondi tra richieste e 4 richieste ogni 15 secondi. Gestisce HTTP 429 con un'attesa, mostra gli errori del dispositivo e interrompe una richiesta HTTP senza risposta dopo 5 secondi. Il pulsante e disabilitato durante lo scan per evitare operazioni sovrapposte.

## Stato corrente

`GET /api/status` include l'oggetto `wifi_diag`: contatori disconnessioni/IP/reconnect, primo e ultimo motivo dell'interruzione, RSSI della disconnessione, ultimo BSSID/canale, durata offline/ultima interruzione e stato/esito scan. I contatori ripartono a ogni boot; gli eventi del buglog restano in NVS. BSSID e RSSI possono riferirsi all'ultimo collegamento conosciuto se una nuova associazione fallisce.

`event_drops` conta eventi persi dalla coda tra callback e loop; `log_drops` conta record diagnostici che non sono stati accettati dalla coda del buglog neppure dopo un tentativo di scaricamento. Questi contatori non certificano il successo fisico di ogni scrittura NVS.

La callback di rete copia soltanto eventi in una coda non bloccante. Formattazione, lettura dello stato e scritture NVS avvengono da setup/loop; il codice non registra password, token o elenchi SSID nei nuovi eventi persistenti.

## Dopo un altro episodio

1. Recuperare `/api/buglog`, `/api/boot-history?limit=12` e `/api/status`, appena il dispositivo e raggiungibile.
2. Conservare i log prima di pulirli o ripetere molti scan. Un riavvio cancella i log RAM, ma non quelli persistenti.
3. Confrontare il motivo `1700`, i retry `1703` e l'eventuale sequenza `1710`/`1711`/`1712`, tenendo separati i boot: `uptime_ms` riparte da zero.

La politica di riconnessione, i GPIO e la tabella partizioni non cambiano in questa versione. Le callback seguono il [modello degli eventi Arduino-ESP32](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/wifi.html#on-event-and-removeevent).

## Verifica della dashboard senza hardware

Eseguire `node --test tests/wifi_scan_ui.test.cjs`. I test estraggono la funzione effettiva dallo sketch e verificano completamento, intervalli di polling, HTTP 429, errori del device, timeout HTTP, doppio clic e numero massimo di richieste. La radio e NVS richiedono verifica sul dispositivo dopo il flash.
