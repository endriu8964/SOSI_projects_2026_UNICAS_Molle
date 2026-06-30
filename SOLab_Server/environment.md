# Ambiente di sviluppo

## Sistema operativo

- **Ubuntu 24.04 LTS** (kernel Linux, x86-64).
  Il progetto è portabile su qualunque distribuzione Linux recente con un
  compilatore C conforme a C11 e supporto POSIX (pthread, IPC System V).

## Strumenti richiesti

| Strumento        | Versione usata | Scopo                                   |
|------------------|----------------|-----------------------------------------|
| `gcc`            | 13.x           | compilazione del codice C               |
| `make`           | 4.x            | gestione della build                    |
| `libc` / pthread | glibc          | thread POSIX, code/memoria condivisa IPC |

## Comandi di installazione

Su Ubuntu/Debian gli strumenti necessari si installano con:

```bash
sudo apt-get update
sudo apt-get install build-essential
```

Il pacchetto `build-essential` include `gcc`, `make` e le librerie di sviluppo
della libc. Il supporto a pthread è incluso nella libc e viene collegato con
`-lpthread` (già presente nel Makefile).

## Strumenti utili per il collaudo

Per verificare l'assenza di risorse IPC orfane e di processi zombie:

```bash
ipcs            # elenco di code di messaggi, memoria condivisa e semafori
ipcrm           # rimozione manuale di risorse IPC eventualmente residue
ps              # verifica della presenza di processi <defunct>
```

## Compilazione

```bash
make
```

Nessun warning con `gcc -Wall`.
