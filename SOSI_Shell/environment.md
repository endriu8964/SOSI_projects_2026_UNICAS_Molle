# Ambiente di sviluppo

Documentazione dell'ambiente in cui il progetto **SOSI Shell** è stato
sviluppato, compilato e testato.

## Sistema operativo

- **Distribuzione:** Ubuntu 24.04 LTS (x86_64)
- **Kernel:** Linux 6.x

> Nota: il progetto è compatibile con Ubuntu 22.04 LTS o superiore, come da
> indicazioni del docente.

## Toolchain

| Strumento | Versione | Note |
|-----------|----------|------|
| `gcc` | 13.x | Compilatore C |
| `make` | 4.3 | Automazione della build |
| `valgrind` | — | Analisi di memory leak (verifica facoltativa) |
| `man-pages` (`manpages-dev`) | — | Consultazione delle system call |

## Preparazione dell'ambiente

```bash
sudo apt update
sudo apt install build-essential manpages-dev
# facoltativo, per la verifica dei memory leak
sudo apt install valgrind
```

## Compilazione

```bash
gcc -Wall -Wextra -g -o sosi_shell SOSI_shell.c
```

oppure tramite `make`.

## Verifica della correttezza della memoria (facoltativa)

```bash
valgrind --leak-check=full ./sosi_shell
```

Esito atteso: *All heap blocks were freed -- no leaks are possible*,
*ERROR SUMMARY: 0 errors*.

## Standard del linguaggio

Il codice usa funzioni POSIX (`fork`, `execv`, `waitpid`, `pipe`, `dup2`,
`stat`, `chdir`, `getcwd`, `gethostname`, `strtok_r`). La compilazione avviene
con il dialetto GNU C predefinito di `gcc`.
