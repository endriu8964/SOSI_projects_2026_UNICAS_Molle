# Ambiente di sviluppo e test — KernelExplorer

## Sistema operativo

- **Distribuzione**: Ubuntu 22.04 LTS (virtual machine)
- **Kernel di test**: 5.15.x — `uname -r`
- **Architettura**: aarch64 (Apple Silicon, VM Multipass su macOS)

> Il modulo è stato scritto per essere portabile fra kernel 5.4 (Ubuntu 20.04)
> e kernel ≥ 5.15 (Ubuntu 22.04/24.04) grazie a una macro condizionale che
> seleziona `task->state` o `task->__state` a seconda della versione del
> kernel (vedi `LINUX_VERSION_CODE` in `kexplorer.c`).

## Pacchetti richiesti

```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r)
```

## Toolchain

- **Compilatore**: `gcc` (versione fornita da `build-essential`)
- **Sistema di build**: `make` con `kbuild` (incluso negli `linux-headers`)

## Strumenti utilizzati per i test

- `dmesg` — lettura del kernel log
- `insmod`, `rmmod`, `lsmod` — gestione moduli
- `ps -el`, `ps -eLf` — confronto con l'output di `list` e `tree`
- `kill`, `sleep` — preparazione dei test di robustezza per il comando `task`
- `wc -l` — conteggio righe per il confronto numerico con `ps`

## Strumenti di IA usati durante lo sviluppo

- **Claude Opus 4.7** (Anthropic) — assistenza nella stesura del codice e
  della relazione. Dettagli nella Parte III della relazione.

## Note operative

- Test eseguiti in una VM dedicata: un modulo kernel mal scritto può
  destabilizzare il sistema, quindi gli esperimenti non vanno mai svolti
  sul sistema host.
- Prima di ogni sessione di test si esegue `sudo dmesg -c` per pulire il
  buffer del kernel log e isolare i messaggi prodotti dal modulo.
