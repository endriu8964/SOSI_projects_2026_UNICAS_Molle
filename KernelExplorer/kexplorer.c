/**
 * kexplorer.c - Modulo kernel interattivo per l'esplorazione del sistema operativo
 *
 * Progetto KernelExplorer
 * Corso di Sistemi Operativi
 *
 * Questo modulo crea un'entry /proc/kexplorer che accetta comandi in scrittura
 * e restituisce i risultati in lettura. I comandi supportati sono:
 *
 *   info           - Mostra informazioni kernel (jiffies, HZ, uptime, GOLDEN_RATIO_PRIME)
 *   gcd A B        - Calcola il massimo comun divisore di A e B
 *   task PID       - Mostra le informazioni del processo con il PID indicato
 *   list           - Elenca tutti i processi (iterazione lineare)
 *   tree           - Attraversa l'albero dei processi in profondita' (DFS)
 *   collatz N      - Genera e stampa la sequenza di Collatz a partire da N
 *
 * Uso:
 *   echo "info" > /proc/kexplorer
 *   cat /proc/kexplorer
 *
 * Compilazione:
 *   make
 *
 * Caricamento:
 *   sudo insmod kexplorer.ko [buffer_size=8192]
 *
 * Rimozione:
 *   sudo rmmod kexplorer
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>       /* copy_from_user, copy_to_user */
#include <linux/slab.h>          /* kmalloc, kfree               */
#include <linux/jiffies.h>       /* jiffies, HZ                  */
#include <linux/gcd.h>           /* gcd()                        */
#include <linux/hash.h>          /* GOLDEN_RATIO_PRIME            */
#include <linux/sched.h>         /* task_struct, for_each_process */
#include <linux/sched/signal.h>  /* for_each_process (kernel 4.11+) */
#include <linux/pid.h>           /* find_vpid, pid_task           */
#include <linux/list.h>          /* list_head, list_for_each, ... */
#include <linux/moduleparam.h>   /* module_param                  */
#include <linux/version.h>       /* LINUX_VERSION_CODE            */
#include <linux/sched/task.h>    /* init_task                     */

#define PROC_NAME "kexplorer"
#define MAX_CMD_LEN 256

/*
 * A partire dal kernel 5.14 il campo "state" della task_struct e' stato
 * rinominato in "__state" e ha cambiato tipo (da volatile long a unsigned int).
 * Per garantire la portabilita' del modulo su kernel 5.4 (Ubuntu 20.04) e
 * 5.15+ (Ubuntu 22.04/24.04), usiamo una macro che seleziona il campo
 * corretto in base alla versione del kernel rilevata in compilazione.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 14, 0)
#define KE_TASK_STATE(t) ((long)((t)->__state))
#else
#define KE_TASK_STATE(t) ((long)((t)->state))
#endif

/*
 * Messaggio standard di troncamento, usato sia da cmd_list che da cmd_tree
 * quando il buffer di output e' pieno e l'iterazione viene interrotta.
 */
#define TRUNC_MSG "[output truncated, buffer full]\n"

/* ========================================================================
 * PARAMETRO DEL MODULO
 * ========================================================================
 * Il parametro buffer_size controlla la dimensione del buffer di output.
 * Puo' essere specificato al caricamento del modulo:
 *   sudo insmod kexplorer.ko buffer_size=8192
 *
 * Se non specificato, viene usato il valore di default (4096).
 */
static int buffer_size = 4096;
module_param(buffer_size, int, 0);
MODULE_PARM_DESC(buffer_size, "Dimensione del buffer di output in byte (default: 4096)");

/* ========================================================================
 * VARIABILI GLOBALI
 * ========================================================================*/

/* Buffer di output: contiene il risultato dell'ultimo comando eseguito.
 * Viene allocato dinamicamente in kexplorer_init e deallocato in kexplorer_exit. */
static char *output_buffer;

/* Lunghezza attuale del contenuto nel buffer di output */
static int output_len;

/* Flag per la gestione della lettura da /proc.
 * Quando completed == 1, proc_read restituisce 0 (EOF) per segnalare
 * che tutti i dati sono stati letti. Viene resettato a 0 dopo ogni
 * nuova scrittura di comando. */
static int completed;

/* Valore di jiffies al momento del caricamento del modulo.
 * Serve per calcolare l'uptime del modulo nel comando "info". */
static unsigned long jiffies_at_load;

/* Entry nel filesystem /proc */
static struct proc_dir_entry *proc_entry;


/* ========================================================================
 * FASE 4 - STRUTTURA PER LA LINKED LIST DI COLLATZ
 * ========================================================================
 *
 * TODO [FASE 4]: Definire la struttura collatz_node.
 *
 * La struttura deve contenere:
 *   - un campo 'value' di tipo unsigned long per il valore numerico
 *   - un campo 'list' di tipo struct list_head per il collegamento nella lista
 *
 * Esempio di definizione di una struttura con list_head (dal testo):
 *
 *   struct color {
 *       int red;
 *       int blue;
 *       int green;
 *       struct list_head list;
 *   };
 *
 * Definire la struttura collatz_node in modo analogo qui sotto:
 */

/* struct collatz_node {
 *     ...
 * };
 */
struct collatz_node {
    unsigned long value;        /* valore numerico della sequenza */
    struct list_head list;      /* link "intrusivo" alla linked list del kernel */
};


/* ========================================================================
 * COMANDO INFO (FASE 1) - GIA' IMPLEMENTATO COME ESEMPIO
 * ========================================================================
 *
 * Questo comando e' fornito come esempio completo di implementazione.
 * Mostra: GOLDEN_RATIO_PRIME, jiffies corrente, HZ e uptime del modulo.
 *
 * Nota: output_len viene aggiornato usando snprintf per scrivere nel buffer.
 * La funzione snprintf restituisce il numero di caratteri scritti.
 * Il parametro (buffer_size - output_len) garantisce di non eccedere il buffer.
 */
static void cmd_info(void)
{
    unsigned long current_jiffies = jiffies;
    unsigned long elapsed_seconds = (current_jiffies - jiffies_at_load) / HZ;

    output_len = 0;
    output_len += snprintf(output_buffer + output_len, buffer_size - output_len,
        "=== KernelExplorer Info ===\n");
    output_len += snprintf(output_buffer + output_len, buffer_size - output_len,
        "GOLDEN_RATIO_PRIME: %lu\n", GOLDEN_RATIO_PRIME);
    output_len += snprintf(output_buffer + output_len, buffer_size - output_len,
        "Current jiffies:    %lu\n", current_jiffies);
    output_len += snprintf(output_buffer + output_len, buffer_size - output_len,
        "HZ (tick rate):     %d\n", HZ);
    output_len += snprintf(output_buffer + output_len, buffer_size - output_len,
        "Module uptime:      %lu seconds\n", elapsed_seconds);
}


/* ========================================================================
 * COMANDO GCD (FASE 1)
 * ========================================================================
 *
 * TODO [FASE 1]: Implementare il comando gcd.
 *
 * La funzione riceve due argomenti (stringhe) che rappresentano numeri interi.
 * Deve:
 *   1. Convertire le stringhe in unsigned long usando kstrtoul().
 *      Nota: kstrtoul(str, 10, &val) converte la stringa 'str' in base 10
 *      e salva il risultato in 'val'. Restituisce 0 in caso di successo.
 *   2. Calcolare il GCD usando la funzione kernel gcd(a, b) da <linux/gcd.h>.
 *   3. Scrivere il risultato nel buffer di output con snprintf.
 *   4. Gestire i casi di errore (argomenti non validi).
 *
 * Formato di output atteso:
 *   GCD(3300, 24) = 12
 *
 * Parametri:
 *   arg1 - stringa contenente il primo numero
 *   arg2 - stringa contenente il secondo numero (puo' essere NULL)
 */
static void cmd_gcd(const char *arg1, const char *arg2)
{
    output_len = 0;

    /* Verificare che entrambi gli argomenti siano presenti */
    if (!arg1 || !arg2) {
        output_len += snprintf(output_buffer, buffer_size,
            "Error: usage: gcd <number1> <number2>\n");
        return;
    }

    /* TODO [FASE 1]: Completare l'implementazione qui.
     *
     * Suggerimenti:
     *   unsigned long a, b, result;
     *   - Usare kstrtoul(arg1, 10, &a) per convertire il primo argomento
     *   - Usare kstrtoul(arg2, 10, &b) per convertire il secondo argomento
     *   - Verificare il valore di ritorno di kstrtoul (0 = successo)
     *   - Calcolare result = gcd(a, b)
     *   - Scrivere nel buffer: output_len += snprintf(...)
     */
    {
        unsigned long a, b, result;

        /* Conversione degli argomenti da stringa a unsigned long in base 10.
         * kstrtoul ritorna 0 in caso di successo, un codice di errore < 0
         * altrimenti (es. -EINVAL per stringa non numerica, -ERANGE per
         * overflow). Lo controlliamo prima di usare i valori. */
        if (kstrtoul(arg1, 10, &a) != 0) {
            output_len += snprintf(output_buffer, buffer_size,
                "Error: invalid number '%s'\n", arg1);
            return;
        }
        if (kstrtoul(arg2, 10, &b) != 0) {
            output_len += snprintf(output_buffer, buffer_size,
                "Error: invalid number '%s'\n", arg2);
            return;
        }

        /* Invocazione della funzione kernel gcd() definita in <linux/gcd.h>.
         * gcd() restituisce 0 quando entrambi gli argomenti sono 0; il caso
         * (0, x) o (x, 0) restituisce x, come da convenzione matematica. */
        result = gcd(a, b);

        output_len += snprintf(output_buffer, buffer_size,
            "GCD(%lu, %lu) = %lu\n", a, b, result);
    }
}


/* ========================================================================
 * COMANDO TASK (FASE 2)
 * ========================================================================
 *
 * TODO [FASE 2]: Implementare il comando task.
 *
 * La funzione riceve un argomento (stringa) che rappresenta un PID.
 * Deve:
 *   1. Convertire la stringa in un intero con kstrtol() (nota: kstrtol,
 *      non kstrtoul, perche' il PID puo' essere negativo per errore).
 *   2. Ottenere la struct pid corrispondente con find_vpid(pid_value).
 *   3. Ottenere la task_struct con pid_task(pid_struct, PIDTYPE_PID).
 *   4. Se pid_task restituisce NULL, stampare un messaggio di errore.
 *   5. Altrimenti, stampare i campi: task->comm, task->pid, task->state.
 *
 * Formato di output atteso:
 *   command = [bash] pid = [1395] state = [1]
 *
 * Funzioni kernel utili:
 *   struct pid *find_vpid(int nr);
 *   struct task_struct *pid_task(struct pid *pid, enum pid_type type);
 *
 * Campi della task_struct (consultare <linux/sched.h>):
 *   task->comm   : nome del comando (char array)
 *   task->pid    : PID del processo (pid_t, cioe' int)
 *   task->state  : stato del processo (long) - in kernel recenti: task->__state
 *
 * Parametri:
 *   arg - stringa contenente il PID
 */
static void cmd_task(const char *arg)
{
    output_len = 0;

    if (!arg) {
        output_len += snprintf(output_buffer, buffer_size,
            "Error: usage: task <pid>\n");
        return;
    }

    /* TODO [FASE 2]: Completare l'implementazione qui.
     *
     * Suggerimenti:
     *   long pid_value;
     *   struct pid *pid_struct;
     *   struct task_struct *task;
     *
     *   - Convertire: kstrtol(arg, 10, &pid_value)
     *   - Trovare: pid_struct = find_vpid((int)pid_value)
     *   - Ottenere: task = pid_task(pid_struct, PIDTYPE_PID)
     *   - Controllare: if (task == NULL) { ... errore ... }
     *   - Stampare: snprintf(... "command = [%s] pid = [%d] state = [%ld]",
     *                          task->comm, task->pid, task->state)
     */
    {
        long pid_value;
        struct pid *pid_struct;
        struct task_struct *task;

        /* Conversione PID. Usiamo kstrtol (non kstrtoul) per intercettare
         * esplicitamente i PID negativi, che non sono validi. */
        if (kstrtol(arg, 10, &pid_value) != 0) {
            output_len += snprintf(output_buffer, buffer_size,
                "Error: '%s' is not a valid integer\n", arg);
            return;
        }

        if (pid_value < 0) {
            output_len += snprintf(output_buffer, buffer_size,
                "Error: PID must be non-negative (got %ld)\n", pid_value);
            return;
        }

        /* find_vpid: cerca la struct pid nel namespace PID corrente.
         * Ritorna NULL se il PID non esiste. */
        pid_struct = find_vpid((int)pid_value);
        if (pid_struct == NULL) {
            output_len += snprintf(output_buffer, buffer_size,
                "Error: no task found for PID %ld\n", pid_value);
            return;
        }

        /* pid_task: ricava la task_struct associata alla struct pid.
         * PIDTYPE_PID indica che vogliamo il task identificato dal PID
         * "stretto" (non un PGID o SID). */
        task = pid_task(pid_struct, PIDTYPE_PID);
        if (task == NULL) {
            output_len += snprintf(output_buffer, buffer_size,
                "Error: no task found for PID %ld\n", pid_value);
            return;
        }

        /* Stampa dei campi richiesti dalla specifica: nome del comando,
         * PID e stato. Lo stato viene letto tramite la macro di compat
         * KE_TASK_STATE per supportare sia il vecchio task->state che il
         * nuovo task->__state (kernel >= 5.14). */
        output_len += snprintf(output_buffer, buffer_size,
            "command = [%s] pid = [%d] state = [%ld]\n",
            task->comm, task->pid, KE_TASK_STATE(task));
    }
}


/* ========================================================================
 * COMANDO LIST (FASE 3)
 * ========================================================================
 *
 * TODO [FASE 3]: Implementare il comando list.
 *
 * La funzione deve iterare su tutti i task del sistema usando la macro
 * for_each_process (definita in <linux/sched/signal.h>) e stampare per
 * ciascuno: nome del comando, stato e PID.
 *
 * La macro for_each_process si usa cosi':
 *
 *   struct task_struct *task;
 *   for_each_process(task) {
 *       // 'task' punta al task corrente ad ogni iterazione
 *   }
 *
 * IMPORTANTE: Gestire il troncamento del buffer!
 *   Il numero di processi puo' essere elevato. Ad ogni iterazione,
 *   verificare che ci sia ancora spazio nel buffer. Se lo spazio si
 *   esaurisce, interrompere l'iterazione e aggiungere un messaggio
 *   di troncamento.
 *
 * Formato di output atteso (una riga per task):
 *   [systemd]          pid = 1       state = 1
 *   [kthreadd]         pid = 2       state = 1
 *   ...
 *
 * L'output deve essere confrontabile con: ps -el
 */
static void cmd_list(void)
{
    output_len = 0;

    /* TODO [FASE 3]: Completare l'implementazione qui.
     *
     * Suggerimenti:
     *   struct task_struct *task;
     *   int written;
     *
     *   for_each_process(task) {
     *       written = snprintf(output_buffer + output_len,
     *                          buffer_size - output_len,
     *                          "[%-16s] pid = %-6d state = %ld\n",
     *                          task->comm, task->pid, task->state);
     *
     *       // Controllare se il buffer e' pieno:
     *       if (output_len + written >= buffer_size) {
     *           // Aggiungere messaggio di troncamento e uscire dal loop
     *           break;
     *       }
     *       output_len += written;
     *   }
     */
    {
        struct task_struct *task;
        int written;
        int truncated = 0;
        /* Spazio da riservare a fine buffer per il messaggio di troncamento
         * e il terminatore null, in modo da poterlo sempre accodare. */
        const int reserve = sizeof(TRUNC_MSG); /* include lo '\0' finale */

        for_each_process(task) {
            int space = buffer_size - output_len - reserve;
            if (space <= 0) {
                truncated = 1;
                break;
            }

            /* snprintf ritorna la lunghezza che SAREBBE stata scritta
             * (escluso il '\0'). Se >= space, c'e' stato troncamento. */
            written = snprintf(output_buffer + output_len, space,
                "[%-16s] pid = %-6d state = %ld\n",
                task->comm, task->pid, KE_TASK_STATE(task));

            if (written >= space) {
                /* La riga corrente non ci stava per intero: la scartiamo
                 * (ripristinando il null terminator nel punto giusto) e
                 * usciamo segnalando il troncamento. */
                output_buffer[output_len] = '\0';
                truncated = 1;
                break;
            }
            output_len += written;
        }

        if (truncated) {
            output_len += snprintf(output_buffer + output_len,
                buffer_size - output_len, TRUNC_MSG);
        }
    }
}


/* Flag globale per segnalare il troncamento durante l'attraversamento DFS.
 * Quando viene settato a 1, dfs_iterate interrompe la ricorsione: in questo
 * modo il messaggio di troncamento viene aggiunto una sola volta. */
static int dfs_truncated;


/* ========================================================================
 * COMANDO TREE - FUNZIONE RICORSIVA DFS (FASE 3)
 * ========================================================================
 *
 * TODO [FASE 3]: Implementare la funzione dfs_iterate.
 *
 * Questa funzione ricorsiva attraversa l'albero dei processi in profondita'.
 * Per ogni task:
 *   1. Stampa le informazioni del task corrente nel buffer di output.
 *   2. Itera sui figli del task usando list_for_each sulla lista children.
 *   3. Per ciascun figlio, ottiene la task_struct con list_entry.
 *   4. Si invoca ricorsivamente sul figlio.
 *
 * Struttura della task_struct rilevante:
 *   task->children : lista dei figli (struct list_head)
 *   task->sibling  : collegamento tra fratelli (struct list_head)
 *
 * Macro per l'attraversamento delle liste kernel:
 *
 *   list_for_each(pos, head)
 *     - pos: variabile struct list_head * usata come cursore
 *     - head: puntatore alla testa della lista
 *
 *   list_entry(ptr, type, member)
 *     - ptr: puntatore al list_head corrente
 *     - type: tipo della struttura contenitore (struct task_struct)
 *     - member: nome del campo list_head nella struttura (sibling)
 *
 * Esempio dal testo:
 *
 *   struct list_head *list;
 *   struct task_struct *child;
 *   list_for_each(list, &current_task->children) {
 *       child = list_entry(list, struct task_struct, sibling);
 *       // child punta ora al task figlio
 *   }
 *
 * IMPORTANTE: Controllare lo spazio nel buffer prima di ogni stampa.
 * Se il buffer e' pieno, aggiungere "[output truncated, buffer full]"
 * e interrompere la ricorsione.
 *
 * Parametri:
 *   task - puntatore al task corrente da cui iniziare il DFS
 */
static void dfs_iterate(struct task_struct *task)
{
    /* TODO [FASE 3]: Completare l'implementazione qui.
     *
     * Suggerimenti:
     *   struct task_struct *child;
     *   struct list_head *list;
     *   int written;
     *
     *   // 1. Stampare le info del task corrente
     *   written = snprintf(output_buffer + output_len, buffer_size - output_len,
     *                      "[%-16s] pid = %-6d state = %ld\n",
     *                      task->comm, task->pid, task->state);
     *   if (output_len + written >= buffer_size) {
     *       // buffer pieno: segnalare il troncamento
     *       return;
     *   }
     *   output_len += written;
     *
     *   // 2. Iterare sui figli
     *   list_for_each(list, &task->children) {
     *       child = list_entry(list, struct task_struct, sibling);
     *       dfs_iterate(child);  // chiamata ricorsiva
     *   }
     */
    struct task_struct *child;
    struct list_head *list;
    int written;
    const int reserve = sizeof(TRUNC_MSG);
    int space;

    /* Se un livello superiore ha gia' rilevato il troncamento, l'intera
     * ricorsione si interrompe immediatamente. Cosi' il messaggio di
     * troncamento viene aggiunto una sola volta da cmd_tree. */
    if (dfs_truncated)
        return;

    space = buffer_size - output_len - reserve;
    if (space <= 0) {
        dfs_truncated = 1;
        return;
    }

    /* 1) Stampa del task corrente */
    written = snprintf(output_buffer + output_len, space,
        "[%-16s] pid = %-6d state = %ld\n",
        task->comm, task->pid, KE_TASK_STATE(task));

    if (written >= space) {
        /* Riga troncata: scartiamo, ripristiniamo il '\0' e segnaliamo. */
        output_buffer[output_len] = '\0';
        dfs_truncated = 1;
        return;
    }
    output_len += written;

    /* 2) Ricorsione sui figli del task corrente.
     *    task->children e' la testa della lista dei figli. Ogni figlio e'
     *    agganciato a quella lista tramite il proprio campo "sibling".
     *    Per ottenere il task_struct dal list_head usiamo list_entry
     *    specificando il tipo contenitore e il nome del campo agganciato. */
    list_for_each(list, &task->children) {
        child = list_entry(list, struct task_struct, sibling);
        dfs_iterate(child);
        if (dfs_truncated)
            return;
    }
}

/**
 * cmd_tree - Handler del comando "tree"
 *
 * Avvia l'attraversamento DFS dall'init_task (il processo radice del sistema).
 * La variabile init_task e' una variabile globale del kernel di tipo
 * struct task_struct che rappresenta il processo swapper (PID 0).
 */
static void cmd_tree(void)
{
    output_len = 0;

    /* TODO [FASE 3]: Avviare il DFS da init_task.
     *
     * Suggerimento:
     *   dfs_iterate(&init_task);
     */
    dfs_truncated = 0;
    dfs_iterate(&init_task);

    /* Se l'attraversamento e' stato interrotto perche' il buffer si e'
     * riempito, accoda il messaggio di troncamento (una sola volta). */
    if (dfs_truncated) {
        output_len += snprintf(output_buffer + output_len,
            buffer_size - output_len, TRUNC_MSG);
    }
}


/* ========================================================================
 * COMANDO COLLATZ (FASE 4)
 * ========================================================================
 *
 * TODO [FASE 4]: Implementare il comando collatz.
 *
 * La funzione riceve un argomento (stringa) che rappresenta un intero N > 0.
 * Deve:
 *   1. Convertire la stringa in unsigned long.
 *   2. Creare una linked list kernel con LIST_HEAD(collatz_list).
 *   3. Generare la sequenza di Collatz:
 *      - Se N e' pari: N = N / 2
 *      - Se N e' dispari: N = 3 * N + 1
 *      - La sequenza termina quando N raggiunge 1 (incluso)
 *   4. Per ogni elemento della sequenza:
 *      a. Allocare un nodo con kmalloc(sizeof(struct collatz_node), GFP_KERNEL)
 *      b. Inizializzare il campo list con INIT_LIST_HEAD(&node->list)
 *      c. Inserire in coda con list_add_tail(&node->list, &collatz_list)
 *   5. Attraversare la lista con list_for_each_entry e stampare i valori.
 *   6. Deallocare tutti i nodi con list_for_each_entry_safe:
 *
 *      struct collatz_node *ptr, *next;
 *      list_for_each_entry_safe(ptr, next, &collatz_list, list) {
 *          list_del(&ptr->list);
 *          kfree(ptr);
 *      }
 *
 *   7. Stampare nel kernel log il numero di nodi deallocati con printk.
 *
 * Formato di output atteso:
 *   Collatz(15): 15 -> 46 -> 23 -> ... -> 1
 *   (18 elements, memory freed)
 *
 * Parametri:
 *   arg - stringa contenente il valore iniziale N
 */
static void cmd_collatz(const char *arg)
{
    output_len = 0;

    if (!arg) {
        output_len += snprintf(output_buffer, buffer_size,
            "Error: usage: collatz <positive_integer>\n");
        return;
    }

    /* TODO [FASE 4]: Completare l'implementazione qui.
     *
     * Suggerimenti:
     *
     * 1. Dichiarazioni:
     *    unsigned long n;
     *    int count = 0;
     *    LIST_HEAD(collatz_list);     // crea la testa della lista
     *    struct collatz_node *node, *ptr, *next;
     *
     * 2. Conversione: kstrtoul(arg, 10, &n)
     *    - Verificare il valore di ritorno e che n > 0
     *
     * 3. Generazione della sequenza:
     *    while (true) {
     *        node = kmalloc(sizeof(struct collatz_node), GFP_KERNEL);
     *        node->value = n;
     *        INIT_LIST_HEAD(&node->list);
     *        list_add_tail(&node->list, &collatz_list);
     *        count++;
     *        if (n == 1) break;
     *        if (n % 2 == 0)
     *            n = n / 2;
     *        else
     *            n = 3 * n + 1;
     *    }
     *
     * 4. Stampa della sequenza:
     *    output_len += snprintf(..., "Collatz(%lu): ", valore_iniziale);
     *    list_for_each_entry(ptr, &collatz_list, list) {
     *        // stampare ptr->value, con " -> " tra gli elementi
     *    }
     *
     * 5. Deallocazione:
     *    list_for_each_entry_safe(ptr, next, &collatz_list, list) {
     *        list_del(&ptr->list);
     *        kfree(ptr);
     *    }
     *    printk(KERN_INFO "kexplorer: Collatz - %d nodes freed\n", count);
     *
     * 6. Aggiungere al buffer: "(%d elements, memory freed)\n"
     */
    {
        unsigned long n, start;
        int count = 0;
        int first = 1;
        int truncated = 0;
        const int reserve = sizeof(TRUNC_MSG);
        LIST_HEAD(collatz_list);
        struct collatz_node *node, *ptr, *next;

        /* --- 1) Validazione dell'input ----------------------------------- */
        if (kstrtoul(arg, 10, &n) != 0) {
            output_len += snprintf(output_buffer, buffer_size,
                "Error: '%s' is not a valid positive integer\n", arg);
            return;
        }
        if (n == 0) {
            output_len += snprintf(output_buffer, buffer_size,
                "Error: collatz argument must be > 0\n");
            return;
        }
        start = n;

        /* --- 2) Generazione della sequenza nella linked list ------------- */
        while (1) {
            node = kmalloc(sizeof(struct collatz_node), GFP_KERNEL);
            if (!node) {
                /* Allocazione fallita: rilasciamo i nodi gia' inseriti per
                 * evitare un memory leak e segnaliamo l'errore. */
                struct collatz_node *p, *nx;
                list_for_each_entry_safe(p, nx, &collatz_list, list) {
                    list_del(&p->list);
                    kfree(p);
                }
                output_len = snprintf(output_buffer, buffer_size,
                    "Error: kmalloc failed during collatz sequence\n");
                printk(KERN_ERR "kexplorer: kmalloc failed in cmd_collatz\n");
                return;
            }
            node->value = n;
            INIT_LIST_HEAD(&node->list);
            list_add_tail(&node->list, &collatz_list);
            count++;

            if (n == 1)
                break;
            if (n % 2 == 0)
                n = n / 2;
            else
                n = 3 * n + 1;
        }

        /* --- 3) Stampa della sequenza nel buffer di output --------------- */
        output_len += snprintf(output_buffer + output_len,
            buffer_size - output_len, "Collatz(%lu): ", start);

        list_for_each_entry(ptr, &collatz_list, list) {
            int written, space;
            const char *sep = first ? "" : " -> ";
            space = buffer_size - output_len - reserve;
            if (space <= 0) {
                truncated = 1;
                break;
            }
            written = snprintf(output_buffer + output_len, space,
                "%s%lu", sep, ptr->value);
            if (written >= space) {
                /* Spazio esaurito a meta' di un numero: scartiamo la
                 * scrittura parziale e segnaliamo il troncamento. */
                output_buffer[output_len] = '\0';
                truncated = 1;
                break;
            }
            output_len += written;
            first = 0;
        }
        /* Newline finale della sequenza (se c'e' spazio). */
        if (!truncated && (buffer_size - output_len) > 1)
            output_len += snprintf(output_buffer + output_len,
                buffer_size - output_len, "\n");

        if (truncated)
            output_len += snprintf(output_buffer + output_len,
                buffer_size - output_len, "\n" TRUNC_MSG);

        /* --- 4) Deallocazione completa della lista ----------------------- *
         * list_for_each_entry_safe consente di rimuovere il nodo corrente
         * durante l'iterazione senza invalidare il cursore (mantiene un
         * puntatore al nodo successivo in 'next'). */
        list_for_each_entry_safe(ptr, next, &collatz_list, list) {
            list_del(&ptr->list);
            kfree(ptr);
        }

        /* --- 5) Riga finale di riepilogo e log kernel -------------------- */
        if (!truncated)
            output_len += snprintf(output_buffer + output_len,
                buffer_size - output_len,
                "(%d elements, memory freed)\n", count);

        printk(KERN_INFO "kexplorer: Collatz(%lu) - %d nodes freed\n",
               start, count);
    }
}


/* ========================================================================
 * DISPATCHER DEI COMANDI
 * ========================================================================
 *
 * Questa funzione analizza il comando ricevuto dall'utente e invoca
 * la funzione handler corrispondente. Il parsing e' gia' implementato.
 *
 * Il formato del comando e':
 *   <comando> [argomento1] [argomento2]
 *
 * Esempi:
 *   "info"           -> cmd = "info",  arg1 = NULL,   arg2 = NULL
 *   "gcd 3300 24"    -> cmd = "gcd",   arg1 = "3300", arg2 = "24"
 *   "task 1395"      -> cmd = "task",  arg1 = "1395", arg2 = NULL
 *   "collatz 15"     -> cmd = "collatz", arg1 = "15", arg2 = NULL
 */
static void dispatch_command(char *input)
{
    char *cmd, *arg1, *arg2;
    char *token, *rest;

    /* Rimuove il newline finale, se presente */
    rest = input;
    token = strsep(&rest, "\n");
    if (!token)
        return;

    /* Dopo aver rimosso il newline, il comando e' in token.
     * Reimpostiamo rest per parsare da token, non dalla parte
     * dopo il newline (che e' vuota). */
    rest = token;

    /* Estrae il comando (prima parola) */
    cmd = strsep(&rest, " ");
    if (!cmd || strlen(cmd) == 0) {
        output_len = snprintf(output_buffer, buffer_size,
            "Error: empty command. Available commands: info, gcd, task, list, tree, collatz\n");
        return;
    }

    /* Estrae il primo argomento (seconda parola), se presente */
    arg1 = strsep(&rest, " ");
    /* Estrae il secondo argomento (terza parola), se presente */
    arg2 = strsep(&rest, " ");

    /* Dispatch del comando alla funzione handler corretta */
    if (strcmp(cmd, "info") == 0) {
        cmd_info();
    } else if (strcmp(cmd, "gcd") == 0) {
        cmd_gcd(arg1, arg2);
    } else if (strcmp(cmd, "task") == 0) {
        cmd_task(arg1);
    } else if (strcmp(cmd, "list") == 0) {
        cmd_list();
    } else if (strcmp(cmd, "tree") == 0) {
        cmd_tree();
    } else if (strcmp(cmd, "collatz") == 0) {
        cmd_collatz(arg1);
    } else {
        output_len = snprintf(output_buffer, buffer_size,
            "Error: unknown command '%s'. Available: info, gcd, task, list, tree, collatz\n",
            cmd);
    }
}


/* ========================================================================
 * FUNZIONI /proc: READ e WRITE
 * ========================================================================
 *
 * Queste due funzioni gestiscono la comunicazione bidirezionale tra lo
 * spazio utente e il modulo kernel attraverso il file /proc/kexplorer.
 *
 * proc_write: viene invocata quando l'utente scrive nel file
 *   (es. echo "info" > /proc/kexplorer)
 *
 * proc_read: viene invocata quando l'utente legge dal file
 *   (es. cat /proc/kexplorer)
 */

/**
 * proc_read - Restituisce il contenuto del buffer di output allo spazio utente.
 *
 * Questa funzione viene chiamata ripetutamente dal kernel finche' non
 * restituisce 0 (EOF). Il flag 'completed' garantisce che i dati vengano
 * inviati una sola volta per ogni comando eseguito.
 *
 * @file:    puntatore alla struttura file (non usato)
 * @usr_buf: buffer nello spazio utente dove copiare i dati
 * @count:   numero massimo di byte che l'utente vuole leggere
 * @pos:     offset corrente nel file (non usato)
 *
 * Return: numero di byte copiati, oppure 0 per segnalare EOF
 */
static ssize_t proc_read(struct file *file, char __user *usr_buf,
                         size_t count, loff_t *pos)
{
    /* Se i dati sono gia' stati letti, restituisce 0 (EOF) */
    if (completed) {
        completed = 0;
        return 0;
    }

    completed = 1;

    /* Controlla che la lunghezza non superi quanto richiesto */
    if (output_len > count)
        output_len = count;

    /* Copia il buffer dal kernel space allo user space.
     * copy_to_user restituisce il numero di byte NON copiati (0 = successo). */
    if (copy_to_user(usr_buf, output_buffer, output_len)) {
        printk(KERN_ERR "kexplorer: copy_to_user failed\n");
        return -EFAULT;
    }

    return output_len;
}

/**
 * proc_write - Riceve un comando dallo spazio utente e lo esegue.
 *
 * Questa funzione:
 *   1. Alloca un buffer temporaneo in kernel memory con kmalloc
 *   2. Copia il comando dallo spazio utente con copy_from_user
 *   3. Aggiunge il terminatore di stringa
 *   4. Passa il comando al dispatcher
 *   5. Libera il buffer temporaneo con kfree
 *
 * @file:    puntatore alla struttura file (non usato)
 * @usr_buf: buffer nello spazio utente contenente il comando
 * @count:   lunghezza del comando in byte
 * @pos:     offset corrente nel file (non usato)
 *
 * Return: numero di byte ricevuti (count)
 */
static ssize_t proc_write(struct file *file, const char __user *usr_buf,
                          size_t count, loff_t *pos)
{
    char *k_mem;

    /* Limita la lunghezza del comando */
    if (count > MAX_CMD_LEN)
        count = MAX_CMD_LEN;

    /* Alloca memoria kernel per ricevere il comando */
    k_mem = kmalloc(count + 1, GFP_KERNEL);
    if (!k_mem) {
        printk(KERN_ERR "kexplorer: kmalloc failed in proc_write\n");
        return -ENOMEM;
    }

    /* Copia il comando dallo spazio utente al kernel */
    if (copy_from_user(k_mem, usr_buf, count)) {
        printk(KERN_ERR "kexplorer: copy_from_user failed\n");
        kfree(k_mem);
        return -EFAULT;
    }

    /* Aggiunge il terminatore di stringa */
    k_mem[count] = '\0';

    /* Resetta il flag di completamento per la prossima lettura */
    completed = 0;

    /* Passa il comando al dispatcher */
    dispatch_command(k_mem);

    /* Libera la memoria temporanea */
    kfree(k_mem);

    return count;
}


/* ========================================================================
 * STRUTTURA file_operations
 * ========================================================================
 *
 * Questa struttura collega le funzioni proc_read e proc_write all'entry
 * nel filesystem /proc. Quando l'utente legge o scrive su /proc/kexplorer,
 * il kernel invoca automaticamente le funzioni indicate qui.
 */
static const struct proc_ops proc_ops = {
    .proc_read  = proc_read,
    .proc_write = proc_write,
};


/* ========================================================================
 * INIT e EXIT DEL MODULO
 * ========================================================================*/

/**
 * kexplorer_init - Funzione di inizializzazione del modulo (entry point).
 *
 * Viene invocata quando il modulo viene caricato con insmod.
 * Crea l'entry /proc/kexplorer, alloca il buffer di output e
 * registra il valore iniziale di jiffies.
 *
 * Return: 0 in caso di successo, codice di errore negativo altrimenti
 */
static int __init kexplorer_init(void)
{
    /* Salva il valore di jiffies al caricamento */
    jiffies_at_load = jiffies;

    /* Alloca il buffer di output */
    output_buffer = kmalloc(buffer_size, GFP_KERNEL);
    if (!output_buffer) {
        printk(KERN_ERR "kexplorer: failed to allocate output buffer\n");
        return -ENOMEM;
    }

    /* Crea l'entry /proc/kexplorer */
    proc_entry = proc_create(PROC_NAME, 0666, NULL, &proc_ops);
    if (!proc_entry) {
        printk(KERN_ERR "kexplorer: failed to create /proc/%s\n", PROC_NAME);
        kfree(output_buffer);
        return -ENOMEM;
    }

    /* Inizializza il buffer con un messaggio di benvenuto */
    output_len = snprintf(output_buffer, buffer_size,
        "KernelExplorer ready. Commands: info, gcd, task, list, tree, collatz\n");
    completed = 0;

    printk(KERN_INFO "kexplorer: module loaded (buffer_size=%d)\n", buffer_size);
    return 0;
}

/**
 * kexplorer_exit - Funzione di pulizia del modulo (exit point).
 *
 * Viene invocata quando il modulo viene rimosso con rmmod.
 * Rimuove l'entry /proc/kexplorer e dealloca il buffer di output.
 */
static void __exit kexplorer_exit(void)
{
    /* Rimuove l'entry /proc/kexplorer */
    remove_proc_entry(PROC_NAME, NULL);

    /* Dealloca il buffer di output */
    kfree(output_buffer);

    printk(KERN_INFO "kexplorer: module unloaded\n");
}

/* Registra le funzioni di init e exit */
module_init(kexplorer_init);
module_exit(kexplorer_exit);

/* Informazioni sul modulo */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("KernelExplorer - Modulo interattivo per l'esplorazione del kernel");
MODULE_AUTHOR("Corso di Sistemi Operativi");
