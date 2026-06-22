# KernelExplorer

**Studente:** Andrea Molle — Matricola 78101

Modulo kernel Linux interattivo per l'esplorazione delle strutture del sistema
operativo. Espone un'interfaccia testuale tramite `/proc/kexplorer` e supporta
sei comandi che coprono gli argomenti dei Programming Projects dei capitoli 2 e
3 di *Operating System Concepts* (10ª ed., Silberschatz–Galvin–Gagne).

## Comandi supportati

| Comando        | Sintassi                              | Descrizione                                                 | Fase |
|----------------|---------------------------------------|-------------------------------------------------------------|------|
| `info`         | `echo "info" > /proc/kexplorer`       | Mostra `jiffies`, `HZ`, uptime del modulo, `GOLDEN_RATIO_PRIME` | 1 |
| `gcd A B`      | `echo "gcd 3300 24" > /proc/kexplorer`| Calcola il MCD usando la funzione kernel `gcd()`            | 1 |
| `task PID`     | `echo "task 1395" > /proc/kexplorer`  | Mostra `comm`, `pid` e `state` del processo con il PID dato | 2 |
| `list`         | `echo "list" > /proc/kexplorer`       | Elenca tutti i task con `for_each_process`                  | 3 |
| `tree`         | `echo "tree" > /proc/kexplorer`       | Attraversa l'albero dei processi in DFS da `init_task`      | 3 |
| `collatz N`    | `echo "collatz 15" > /proc/kexplorer` | Genera la sequenza di Collatz in una linked list del kernel | 4 |

## Compilazione, caricamento, rimozione

```bash
# compilazione
make

# caricamento (con buffer di default 4096 byte)
sudo insmod kexplorer.ko

# caricamento con buffer custom
sudo insmod kexplorer.ko buffer_size=8192

# verifica caricamento
lsmod | grep kexplorer
ls /proc/kexplorer
dmesg | tail

# esempi di uso
echo "info" > /proc/kexplorer && cat /proc/kexplorer
echo "gcd 3300 24" > /proc/kexplorer && cat /proc/kexplorer
echo "task 1" > /proc/kexplorer && cat /proc/kexplorer
echo "list" > /proc/kexplorer && cat /proc/kexplorer
echo "tree" > /proc/kexplorer && cat /proc/kexplorer
echo "collatz 15" > /proc/kexplorer && cat /proc/kexplorer

# rimozione e pulizia
sudo rmmod kexplorer
make clean
```

## Struttura del progetto

```
KernelExplorer/
├── kexplorer.c               # sorgente del modulo kernel
├── Makefile                  # regole di compilazione
├── README.md                 # questo file
├── environment.md            # dettagli dell'ambiente di sviluppo
├── .gitignore                # esclusioni Git (binari kernel)
└── relazioneKernelExplorer.pdf
```

## Parametri del modulo

| Nome          | Tipo | Default | Descrizione                                  |
|---------------|------|---------|----------------------------------------------|
| `buffer_size` | int  | 4096    | Dimensione (byte) del buffer di output `/proc` |

## Note

Il modulo è scritto per kernel ≥ 5.4 e gestisce sia il vecchio campo
`task->state` (kernel < 5.14) sia il nuovo `task->__state` tramite una macro
di compatibilità basata su `LINUX_VERSION_CODE`.
