# Progetto 3 — SOLab Server

Applicazione client-server concorrente per la gestione di un archivio studenti,
realizzata per il corso di **Sistemi Operativi e Sicurezza Informatica**
(Prof. Francesco Fontanella).

**Autore:** Andrea Molle — matricola 78101

## Descrizione

Il progetto è composto da tre eseguibili:

- **`gen_db`** — utilità che genera il database binario di prova (record `student`
  di dimensione fissa). Fornita dalla traccia, lasciata invariata.
- **`solabd`** — il server: carica il DB in memoria, crea il thread pool e si mette
  in ascolto delle richieste su una coda di messaggi System V.
- **`solabc`** — il client: menu testuale che invia richieste al server e ne mostra
  la risposta.

Sono implementate tutte e 7 le fasi previste dalla traccia: caricamento del DB con
accesso diretto (`lseek`), scan multiprocesso con varianti pipe / memoria condivisa,
server multithread (thread pool e thread-per-richiesta), sincronizzazione
lettori/scrittori (soluzione base e priva di starvation), code di messaggi System V,
politica di ammissione con variabili di condizione e gestione dei segnali.

## Come compilare

Dalla cartella del progetto:

```bash
make            # costruisce solabd, solabc e gen_db (gcc -Wall -O2 -lpthread)
make clean      # rimuove gli eseguibili
```

La compilazione avviene senza alcun warning con `-Wall`.

## Come eseguire

```bash
# 1. generare un database di prova (es. 1000 studenti, seme 42)
./gen_db students.bin 1000 42

# 2. avviare il server (in un terminale)
./solabd students.bin                 # default: thread pool + lettori/scrittori fair
./solabd students.bin --thread-per    # un thread per richiesta (Fase 3a)
./solabd students.bin --base-rw       # lettori/scrittori soluzione base (Fase 4a)
./solabd students.bin --stdin         # ciclo di test interattivo (Fasi 1-4)

# 3. avviare uno o più client (in altri terminali)
./solabc
```

### Segnali gestiti dal server (Fase 7)

| Segnale            | Effetto                                            |
|--------------------|----------------------------------------------------|
| `SIGINT`/`SIGTERM` | terminazione pulita (rimozione coda, rilascio risorse) |
| `SIGUSR1`          | stampa delle statistiche                           |
| `SIGUSR2`          | ricarica del database (sotto lock di scrittura)    |
| `SIGALRM`          | riepilogo periodico (si ri-arma da solo)           |
| `SIGCHLD`          | raccolta dei processi figli dello scan (no zombie) |

## Contenuto della cartella

```
solabd.c        server
solabc.c        client
gen_db.c        generatore del database (invariato)
solab.h         definizioni condivise (struct, costanti, chiave IPC)
Makefile        regole di build
relazioneClientServer.pdf   relazione del progetto
README.md  environment.md  .gitignore
```
