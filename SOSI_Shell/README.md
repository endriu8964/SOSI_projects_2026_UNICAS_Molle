# SOSI Shell

Shell Unix-like semplificata in C, sviluppata per il corso di **Sistemi Operativi
e Sicurezza Informatica** a partire dallo skeleton `SOSI_shell.c`.

## Descrizione

`sosi_shell` è un interprete di comandi interattivo che legge righe da standard
input, le interpreta e le esegue, distinguendo tra comandi *built-in* (eseguiti
direttamente dalla shell) e programmi *esterni* (eseguiti in processi figli).

## Funzionalità implementate

| Fase | Funzionalità |
|------|--------------|
| 0 | Pulizia dello skeleton, allineamento prototipi/firme, compilazione pulita con `-Wall -Wextra` |
| 1 | Prompt informativo `user@host:cwd$`, parsing della riga, built-in `exit`/`quit`/`cd`/`history`, ricerca dell'eseguibile in `PATH` e per path assoluto, esecuzione con `fork`/`execv`/`waitpid`, foreground/background (`&`), redirezione dell'output (`>`) |
| 2 | Cronologia degli ultimi 10 comandi su array circolare, comando `history`, recupero con `!!`, `!N` e (bonus) `!str` |
| 3 | Pipeline semplice tra due comandi (`cmd1 | cmd2`) con `pipe`/`fork`/`dup2`/`close` |

## Compilazione ed esecuzione

```bash
make            # compila (gcc -Wall -Wextra -g)
make run        # compila ed esegue
make debug      # compila con AddressSanitizer
make clean      # rimuove l'eseguibile
```

In alternativa, compilazione manuale:

```bash
gcc -Wall -Wextra -g -o sosi_shell SOSI_shell.c
./sosi_shell
```

## Esempi d'uso

```text
user@host:~$ ls -l /tmp
user@host:~$ cd /etc
user@host:/etc$ cat passwd | grep root
user@host:/etc$ ls -l > listing.txt
user@host:/etc$ sleep 5 &
user@host:/etc$ history
user@host:/etc$ !!
user@host:/etc$ exit
```

## File

- `SOSI_shell.c` — sorgente della shell
- `Makefile` — automazione della build
- `relazioneShell.pdf` — relazione tecnica del progetto

## Limiti noti

- Una sola pipe per riga di comando (pipeline multiple non supportate).
- Non sono gestite le virgolette per argomenti contenenti spazi né il globbing.
- La redirezione dell'input (`<`) e l'append (`>>`) non sono implementati.
