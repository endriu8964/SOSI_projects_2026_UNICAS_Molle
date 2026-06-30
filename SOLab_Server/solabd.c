/* ============================================================================
 *  solabd.c  --  Server del progetto SOLab.
 *
 *  Progetto SOLab Server -- Corso di Sistemi Operativi
 *
 *  Implementazione completa delle 7 fasi:
 *    F1  caricamento DB binario (load) e comando count
 *    F2  comando scan: calcolo parallelo multiprocesso via pipe / shared memory
 *    F3  server multithread: thread-per-request e thread pool con coda dei task
 *    F4  modello lettori/scrittori: soluzione base e variante priva di starvation
 *    F5  comunicazione client/server con code di messaggi System V
 *    F6  politiche di ammissione con condition variables (classi, priorita', N)
 *    F7  gestione dei segnali: terminazione pulita, statistiche, ricarica, no zombie
 *
 *  Uso:
 *    ./solabd <file_db>                 server completo su coda di messaggi (F5)
 *    ./solabd <file_db> --stdin         ciclo di test interattivo (F1-F4 manuali)
 *    opzioni aggiuntive:
 *       --thread-per   usa un thread per richiesta invece del thread pool (F3a)
 *       --base-rw      usa la soluzione lettori/scrittori base invece della fair
 *
 *  Comandi del ciclo di test (--stdin), uno per riga:
 *       count | search id <ID> | search surname <S> | scan <V> [nproc] [pipe|shm]
 *       insert <ID> <cognome> <nome> <voto> | update <ID> <cognome> <nome> <voto>
 *       stats | load | quit
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <pthread.h>        /* thread, mutex                    (Fasi 3,4,6) */
#include <semaphore.h>      /* semafori POSIX                   (Fasi 3,4)   */
#include <signal.h>         /* gestione segnali                 (Fase 7)     */
#include <sys/wait.h>       /* wait/waitpid                     (Fasi 2,7)   */
#include <sys/ipc.h>        /* IPC System V                     (Fasi 2,5)   */
#include <sys/msg.h>        /* code di messaggi                 (Fase 5)     */
#include <sys/shm.h>        /* memoria condivisa                (Fase 2)     */

#include "solab.h"

/* ===========================================================================
 *  STATO GLOBALE DEL SERVER
 * ==========================================================================*/
static student     db[MAX_STUDENTS];   /* database in memoria               */
static int         n_students = 0;     /* numero di record caricati         */
static const char *db_filename = NULL; /* file da cui e' stato caricato      */

/* Statistiche (utili per il comando "stats" e per SIGUSR1, Fase 7). */
static volatile int served_requests = 0;
static volatile int active_readers  = 0;
static volatile int active_writers  = 0;

/* Flag di terminazione, impostato dai gestori di segnale (Fase 7). */
static volatile sig_atomic_t running = 1;

/* Flag asincroni impostati dai gestori (richieste differite al ciclo main). */
static volatile sig_atomic_t want_stats   = 0;  /* SIGUSR1 */
static volatile sig_atomic_t want_reload  = 0;  /* SIGUSR2 */
static volatile sig_atomic_t want_summary = 0;  /* SIGALRM */

/* Selettori di modalita' impostati dalla riga di comando. */
static int opt_use_pool = 1;   /* 1 = thread pool (F3b), 0 = thread-per (F3a) */
static int opt_fair_rw  = 1;   /* 1 = lettori/scrittori fair, 0 = base        */

/* Insieme dei segnali gestiti: bloccato nei thread worker, gestito dal main. */
static sigset_t g_handled_set;

/* Prototipi delle funzioni richiamate prima della definizione. */
static void serve_and_reply(request *r);

/* ===========================================================================
 *  FASE 1 -- INFRASTRUTTURA: caricamento e accesso diretto al file
 * ==========================================================================*/

/* Carica l'intero database dal file binario nell'array arr[].
 * Si usa lseek(SEEK_END) per ottenere la dimensione del file e quindi il numero
 * di record. Restituisce il numero di record caricati. */
static int load(const char *filename, student *arr)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0) { perror("open db"); exit(EXIT_FAILURE); }

    off_t size = lseek(fd, 0, SEEK_END);   /* dimensione del file in byte */
    if (size < 0) { perror("lseek"); close(fd); exit(EXIT_FAILURE); }
    lseek(fd, 0, SEEK_SET);

    int count = (int)(size / (off_t) sizeof(student));
    if (count > MAX_STUDENTS) count = MAX_STUDENTS;

    ssize_t rd = read(fd, arr, (size_t) count * sizeof(student));
    if (rd < 0) { perror("read db"); close(fd); exit(EXIT_FAILURE); }

    close(fd);
    return count;
}

/* ===========================================================================
 *  FASE 4 -- SINCRONIZZAZIONE: il modello lettori/scrittori
 *  Sono implementate DUE soluzioni; la dispatch (read_request/...) sceglie a
 *  runtime quella attiva in base a opt_fair_rw.
 * ==========================================================================*/

/* --- Stato della soluzione BASE (Fase 4.1) ------------------------------- */
static pthread_mutex_t rw_mutex = PTHREAD_MUTEX_INITIALIZER; /* protegge readcount */
static sem_t           wrt;            /* accesso esclusivo agli scrittori   */
static int             readcount = 0;

/* --- Stato della soluzione FAIR / priva di starvation (Fase 4.2) --------- *
 * Tecnica delle "private semaphores": un mutex protegge i contatori; i
 * semafori readers_sem/writers_sem (inizializzati a 0) sospendono i thread.  */
static pthread_mutex_t fair_mutex = PTHREAD_MUTEX_INITIALIZER;
static sem_t           readers_sem, writers_sem;
static int             read_in = 0, wrt_in = 0;    /* presenti nella sezione  */
static int             read_susp = 0, wrt_susp = 0;/* sospesi sul semaforo    */

/* Inizializza mutex e semafori della sincronizzazione. Chiamata dal main. */
static void sync_init(void)
{
    sem_init(&wrt, 0, 1);          /* soluzione base: wrt come lock esclusivo */
    sem_init(&readers_sem, 0, 0);  /* soluzione fair: semafori privati a 0    */
    sem_init(&writers_sem, 0, 0);
    readcount = 0;
    read_in = wrt_in = read_susp = wrt_susp = 0;
}

/* ---- Soluzione BASE ----------------------------------------------------- */
static void rw_read_enter_base(void)
{
    pthread_mutex_lock(&rw_mutex);
    readcount++;
    if (readcount == 1) sem_wait(&wrt);   /* il primo lettore esclude scrittori */
    active_readers = readcount;
    pthread_mutex_unlock(&rw_mutex);
}
static void rw_read_exit_base(void)
{
    pthread_mutex_lock(&rw_mutex);
    readcount--;
    active_readers = readcount;
    if (readcount == 0) sem_post(&wrt);   /* l'ultimo lettore libera scrittori  */
    pthread_mutex_unlock(&rw_mutex);
}
static void rw_write_enter_base(void)
{
    sem_wait(&wrt);                        /* accesso esclusivo                  */
    active_writers = 1;
}
static void rw_write_exit_base(void)
{
    active_writers = 0;
    sem_post(&wrt);
}

/* ---- Soluzione FAIR (priva di starvation) ------------------------------- *
 * - un nuovo lettore si sospende se c'e' uno scrittore dentro o in attesa
 *   (cosi' un flusso continuo di lettori non affama gli scrittori);
 * - uscendo, uno scrittore risveglia PRIMA tutti i lettori sospesi e solo se
 *   non ce ne sono passa il turno a uno scrittore (cosi' gli scrittori non
 *   affamano i lettori). L'alternanza che ne risulta limita l'attesa di
 *   entrambe le classi.                                                       */
static void rw_read_enter_fair(void)
{
    pthread_mutex_lock(&fair_mutex);
    if (wrt_in > 0 || wrt_susp > 0) {
        read_susp++;
        pthread_mutex_unlock(&fair_mutex);
        sem_wait(&readers_sem);            /* risvegliato gia' "dentro"          */
    } else {
        read_in++;
        active_readers = read_in;
        pthread_mutex_unlock(&fair_mutex);
    }
}
static void rw_read_exit_fair(void)
{
    pthread_mutex_lock(&fair_mutex);
    read_in--;
    active_readers = read_in;
    if (read_in == 0 && wrt_susp > 0) {    /* ultimo lettore: passa a uno scrittore */
        wrt_susp--;
        wrt_in++;
        active_writers = 1;
        sem_post(&writers_sem);
    }
    pthread_mutex_unlock(&fair_mutex);
}
static void rw_write_enter_fair(void)
{
    pthread_mutex_lock(&fair_mutex);
    if (read_in > 0 || wrt_in > 0) {
        wrt_susp++;
        pthread_mutex_unlock(&fair_mutex);
        sem_wait(&writers_sem);            /* risvegliato gia' "dentro"          */
    } else {
        wrt_in++;
        active_writers = 1;
        pthread_mutex_unlock(&fair_mutex);
    }
}
static void rw_write_exit_fair(void)
{
    pthread_mutex_lock(&fair_mutex);
    wrt_in--;
    active_writers = 0;
    if (read_susp > 0) {                   /* priorita' a sbloccare i lettori     */
        while (read_susp > 0) {
            read_susp--;
            read_in++;
            sem_post(&readers_sem);
        }
        active_readers = read_in;
    } else if (wrt_susp > 0) {             /* altrimenti il prossimo scrittore    */
        wrt_susp--;
        wrt_in++;
        active_writers = 1;
        sem_post(&writers_sem);
    }
    pthread_mutex_unlock(&fair_mutex);
}

/* ---- Dispatch: protocollo lettori/scrittori effettivamente usato -------- */
static void read_request(void)  { if (opt_fair_rw) rw_read_enter_fair();  else rw_read_enter_base();  }
static void read_end(void)      { if (opt_fair_rw) rw_read_exit_fair();   else rw_read_exit_base();   }
static void write_request(void) { if (opt_fair_rw) rw_write_enter_fair(); else rw_write_enter_base(); }
static void write_end(void)     { if (opt_fair_rw) rw_write_exit_fair();  else rw_write_exit_base();  }

/* ===========================================================================
 *  OPERAZIONI SUL DATABASE (primitive pure, senza locking)
 *  Il locking lettori/scrittori e l'ammissione sono applicati dal livello
 *  superiore (process_request), per non duplicarli tra ciclo stdin e ciclo IPC.
 * ==========================================================================*/

static int op_count(void)
{
    return n_students;
}

/* Ricerca per ID: 1 e copia il record in *out se trovato, 0 altrimenti. */
static int op_search_id(int id, student *out)
{
    for (int i = 0; i < n_students; i++) {
        if (db[i].id == id) {
            if (out) *out = db[i];
            return 1;
        }
    }
    return 0;
}

/* Ricerca per cognome: restituisce il numero di corrispondenze; la prima
 * corrispondenza (se richiesta) viene copiata in *out. */
static int op_search_surname(const char *surname, student *out)
{
    int matches = 0;
    for (int i = 0; i < n_students; i++) {
        if (strncmp(db[i].surname, surname, SURNAME_LEN) == 0) {
            if (matches == 0 && out) *out = db[i];
            matches++;
        }
    }
    return matches;
}

/* Inserimento in coda all'array. Scrittura. 1 se ok, 0 se pieno. */
static int op_insert(student s)
{
    if (n_students >= MAX_STUDENTS) return 0;
    db[n_students++] = s;
    return 1;
}

/* Aggiornamento del record con id == s.id. Scrittura. 1 se trovato, 0 altrim. */
static int op_update(student s)
{
    for (int i = 0; i < n_students; i++) {
        if (db[i].id == s.id) {
            db[i] = s;                 /* copia dell'intero record (id invariato) */
            return 1;
        }
    }
    return 0;
}

/* ===========================================================================
 *  FASE 2 -- CALCOLO PARALLELO MULTIPROCESSO: pipe e memoria condivisa
 * ==========================================================================*/

/* Lettura robusta di n byte (le pipe possono restituire letture parziali). */
static ssize_t read_full(int fd, void *buf, size_t n)
{
    size_t got = 0;
    char  *p = buf;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) break;
        got += (size_t) r;
    }
    return (ssize_t) got;
}

/* Codice eseguito da ciascun figlio: conta nelle posizioni [start, end) del
 * file le occorrenze del voto "val". Apre il file SEPARATAMENTE (puntatore di
 * I/O privato) e si posiziona con lseek a start*sizeof(student). */
static long child_scan(const char *filename, int val, int start, int end)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0) { perror("child open db"); return 0; }
    if (lseek(fd, (off_t) start * (off_t) sizeof(student), SEEK_SET) < 0) {
        perror("child lseek"); close(fd); return 0;
    }
    long cnt = 0;
    student s;
    for (int i = start; i < end; i++) {
        ssize_t r = read(fd, &s, sizeof(s));
        if (r != (ssize_t) sizeof(s)) break;
        if (s.exam_score == val) cnt++;
    }
    close(fd);
    return cnt;
}

/* Comando "scan val [nproc] [pipe|shm]".
 * Ripartisce gli n_students record tra nproc figli (l'ultimo prende il resto),
 * raccoglie i conteggi parziali e restituisce il totale.
 * Per tutta la durata blocca SIGCHLD nel thread chiamante: cosi' il gestore di
 * SIGCHLD (Fase 7) non "ruba" i figli e waitpid li raccoglie in modo
 * deterministico, sia nel ciclo stdin (main) sia in un worker del pool. */
static long op_scan(int val, int nproc, int use_pipe)
{
    int N = n_students;
    if (N <= 0) return 0;
    if (nproc < 1) nproc = 1;
    if (nproc > N) nproc = N;            /* niente figli "a vuoto" (N > N_int) */

    int quota = N / nproc;
    int resto = N % nproc;

    sigset_t blockset, oldset;
    sigemptyset(&blockset);
    sigaddset(&blockset, SIGCHLD);
    pthread_sigmask(SIG_BLOCK, &blockset, &oldset);

    pid_t *pids = malloc((size_t) nproc * sizeof(pid_t));
    if (!pids) { pthread_sigmask(SIG_SETMASK, &oldset, NULL); return 0; }

    long total = 0;

    if (use_pipe) {
        /* ---- Variante PIPE ---- */
        int fd[2];
        if (pipe(fd) < 0) { perror("pipe"); free(pids); pthread_sigmask(SIG_SETMASK,&oldset,NULL); return 0; }
        for (int i = 0; i < nproc; i++) {
            int start = i * quota;
            int end   = (i == nproc - 1) ? (start + quota + resto) : (start + quota);
            pid_t pid = fork();
            if (pid < 0) { perror("fork"); pids[i] = -1; continue; }
            if (pid == 0) {
                close(fd[0]);
                long c = child_scan(db_filename, val, start, end);
                if (write(fd[1], &c, sizeof(c)) != (ssize_t) sizeof(c))
                    perror("child write pipe");
                close(fd[1]);
                _exit(0);
            }
            pids[i] = pid;
        }
        close(fd[1]);                    /* il padre non scrive                 */
        for (int i = 0; i < nproc; i++) {/* la read si sblocca quando i figli scrivono */
            long c = 0;
            if (read_full(fd[0], &c, sizeof(c)) == (ssize_t) sizeof(c)) total += c;
        }
        close(fd[0]);
        for (int i = 0; i < nproc; i++)  /* raccoglie i figli: niente zombie    */
            if (pids[i] > 0) waitpid(pids[i], NULL, 0);

    } else {
        /* ---- Variante SHARED MEMORY ---- */
        int shmid = shmget(IPC_PRIVATE, (size_t) nproc * sizeof(long), IPC_CREAT | 0666);
        if (shmid < 0) { perror("shmget"); free(pids); pthread_sigmask(SIG_SETMASK,&oldset,NULL); return 0; }
        long *res = (long *) shmat(shmid, NULL, 0);
        if (res == (void *) -1) { perror("shmat"); shmctl(shmid, IPC_RMID, NULL); free(pids); pthread_sigmask(SIG_SETMASK,&oldset,NULL); return 0; }
        for (int i = 0; i < nproc; i++) {
            int start = i * quota;
            int end   = (i == nproc - 1) ? (start + quota + resto) : (start + quota);
            pid_t pid = fork();
            if (pid < 0) { perror("fork"); pids[i] = -1; res[i] = 0; continue; }
            if (pid == 0) {
                res[i] = child_scan(db_filename, val, start, end);
                shmdt(res);
                _exit(0);
            }
            pids[i] = pid;
        }
        for (int i = 0; i < nproc; i++)  /* waitpid = barriera di completamento  */
            if (pids[i] > 0) waitpid(pids[i], NULL, 0);
        for (int i = 0; i < nproc; i++) total += res[i];
        shmdt(res);
        shmctl(shmid, IPC_RMID, NULL);   /* nessun segmento orfano (ipcs)        */
    }

    free(pids);
    pthread_sigmask(SIG_SETMASK, &oldset, NULL);
    return total;
}

/* ===========================================================================
 *  FASE 6 -- POLITICHE DI ACCESSO CON CONDITION VARIABLES
 *  Due classi di client (prioritari/ordinari): mutua esclusione tra classi,
 *  priorita' ai prioritari, capacita' massima SERVER_CAPACITY.
 *  (Definita prima del thread pool perche' i worker la richiamano.)
 * ==========================================================================*/
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  ok_priority;   /* attesa dei client prioritari            */
    pthread_cond_t  ok_normal;     /* attesa dei client ordinari              */
    int  in_priority, in_normal;   /* presenti per classe                     */
    int  wait_priority, wait_normal;/* sospesi per classe                     */
} admission_t;

static admission_t adm;

static void admission_init(void)
{
    pthread_mutex_init(&adm.mutex, NULL);
    pthread_cond_init(&adm.ok_priority, NULL);
    pthread_cond_init(&adm.ok_normal, NULL);
    adm.in_priority = adm.in_normal = 0;
    adm.wait_priority = adm.wait_normal = 0;
}

/* Ingresso di un client; is_priority indica la classe. */
static void admission_enter(int is_priority)
{
    pthread_mutex_lock(&adm.mutex);
    if (is_priority) {
        /* attende se ci sono ordinari in servizio o se la capacita' e' piena   */
        while (adm.in_normal > 0 || adm.in_priority >= SERVER_CAPACITY) {
            adm.wait_priority++;
            pthread_cond_wait(&adm.ok_priority, &adm.mutex);
            adm.wait_priority--;
        }
        adm.in_priority++;
    } else {
        /* attende se ci sono prioritari (in servizio o in attesa) o capacita' piena */
        while (adm.in_priority > 0 || adm.wait_priority > 0 || adm.in_normal >= SERVER_CAPACITY) {
            adm.wait_normal++;
            pthread_cond_wait(&adm.ok_normal, &adm.mutex);
            adm.wait_normal--;
        }
        adm.in_normal++;
    }
    pthread_mutex_unlock(&adm.mutex);
}

/* Uscita di un client. */
static void admission_exit(int is_priority)
{
    pthread_mutex_lock(&adm.mutex);
    if (is_priority) adm.in_priority--; else adm.in_normal--;
    /* I predicati nelle while ricontrollano sempre: risvegliare e' sicuro.
     * Priorita' ai prioritari anche al risveglio. */
    if (adm.wait_priority > 0)
        pthread_cond_broadcast(&adm.ok_priority);
    else if (adm.wait_normal > 0)
        pthread_cond_broadcast(&adm.ok_normal);
    pthread_mutex_unlock(&adm.mutex);
}

/* ===========================================================================
 *  FASE 3 -- SERVER MULTITHREAD E THREAD POOL
 * ==========================================================================*/

/* Blocca nel thread chiamante l'insieme dei segnali gestiti dal main:
 * i worker non devono eseguire i gestori di segnale. */
static void block_handled_signals(void)
{
    pthread_sigmask(SIG_BLOCK, &g_handled_set, NULL);
}

/* (a) THREAD-PER-RICHIESTA (Fase 3a).
 * Per ogni richiesta il main crea un thread, passandogli una copia della
 * request allocata con malloc; il thread serve la richiesta, risponde sulla
 * coda e libera la memoria. */
static void *worker_request(void *arg)
{
    block_handled_signals();
    request *rp = (request *) arg;
    serve_and_reply(rp);
    free(rp);
    return NULL;
}

/* (b) THREAD POOL (Fase 3b).
 * Coda dei task protetta da mutex; i worker attendono lavoro su un semaforo
 * (nessun busy waiting). */
typedef struct task {
    request       req;
    struct task  *next;
} task_t;

static task_t         *task_head = NULL, *task_tail = NULL;
static pthread_mutex_t  queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static sem_t            task_avail;          /* conta i task disponibili      */
static pthread_t        pool[POOL_SIZE];
static volatile int     pool_running = 0;
static int              pool_started = 0;   /* 1 dopo pool_init() riuscito     */

static void enqueue(request r)
{
    task_t *t = malloc(sizeof(*t));
    if (!t) { perror("malloc task"); return; }
    t->req = r;
    t->next = NULL;
    pthread_mutex_lock(&queue_mutex);
    if (task_tail) task_tail->next = t; else task_head = t;
    task_tail = t;
    pthread_mutex_unlock(&queue_mutex);
    sem_post(&task_avail);               /* notifica un worker                 */
}

static int dequeue(request *out)
{
    sem_wait(&task_avail);               /* attesa passiva                     */
    pthread_mutex_lock(&queue_mutex);
    if (!task_head) {                    /* svegliato per lo shutdown          */
        pthread_mutex_unlock(&queue_mutex);
        return 0;
    }
    task_t *t = task_head;
    task_head = t->next;
    if (!task_head) task_tail = NULL;
    pthread_mutex_unlock(&queue_mutex);
    *out = t->req;
    free(t);
    return 1;
}

static void *pool_worker(void *arg)
{
    (void) arg;
    block_handled_signals();
    request r;
    while (pool_running) {
        if (!dequeue(&r)) break;         /* 0 = shutdown a coda vuota          */
        serve_and_reply(&r);
    }
    return NULL;
}

static int pool_init(void)
{
    sem_init(&task_avail, 0, 0);
    pool_running = 1;
    for (int i = 0; i < POOL_SIZE; i++) {
        if (pthread_create(&pool[i], NULL, pool_worker, NULL) != 0) {
            perror("pthread_create pool");
            return -1;
        }
    }
    pool_started = 1;
    return 0;
}

static void pool_shutdown(void)
{
    pool_running = 0;
    for (int i = 0; i < POOL_SIZE; i++) sem_post(&task_avail); /* sveglia i worker */
    for (int i = 0; i < POOL_SIZE; i++) pthread_join(pool[i], NULL);
    /* svuota eventuali task residui */
    pthread_mutex_lock(&queue_mutex);
    while (task_head) { task_t *t = task_head; task_head = t->next; free(t); }
    task_tail = NULL;
    pthread_mutex_unlock(&queue_mutex);
    sem_destroy(&task_avail);
}

/* ===========================================================================
 *  ELABORAZIONE DI UNA RICHIESTA
 *  Punto unico in cui si compongono ammissione (F6) + lettori/scrittori (F4)
 *  + operazioni sul DB. Richiamata sia dal pool (F3b) sia dai thread
 *  per-richiesta (F3a).
 * ==========================================================================*/
static void process_request(request *r)
{
    int prio = r->priority ? 1 : 0;

    switch (r->req_kind) {
    case REQ_COUNT:
        admission_enter(prio); read_request();
        r->num = op_count(); r->found = 1;
        read_end(); admission_exit(prio);
        break;

    case REQ_SEARCH_ID:
        admission_enter(prio); read_request();
        r->found = op_search_id(r->id, &r->s);
        read_end(); admission_exit(prio);
        break;

    case REQ_SEARCH_SURNAME:
        admission_enter(prio); read_request();
        r->num = op_search_surname(r->surname, &r->s);
        r->found = (r->num > 0);
        read_end(); admission_exit(prio);
        break;

    case REQ_SCAN: {
        int np = (r->scan_nproc > 0) ? r->scan_nproc : SCAN_NPROC;
        admission_enter(prio);
        long occ = op_scan(r->id, np, r->scan_pipe);  /* legge il file, non db[] */
        admission_exit(prio);
        r->num = (int) occ; r->found = 1;
        break;
    }

    case REQ_INSERT:
        admission_enter(prio); write_request();
        r->found = op_insert(r->s);
        write_end(); admission_exit(prio);
        break;

    case REQ_UPDATE:
        admission_enter(prio); write_request();
        r->found = op_update(r->s);
        write_end(); admission_exit(prio);
        break;

    case REQ_PRIORITY:
        /* il client memorizza la propria classe e la marca su ogni richiesta;
         * il server si limita a confermare. */
        r->found = 1;
        break;

    case REQ_QUIT:
        r->found = 1;
        break;

    default:
        r->found = 0;
        break;
    }
}

/* ===========================================================================
 *  FASE 7 -- GESTIONE DEI SEGNALI
 * ==========================================================================*/
static int  msg_qid = -1;   /* id della coda di messaggi (Fase 5)            */

static void cleanup_resources(void)
{
    if (opt_use_pool && pool_started) pool_shutdown(); /* termina e raccoglie i worker */
    if (msg_qid >= 0) {                         /* rimuove la coda di messaggi   */
        msgctl(msg_qid, IPC_RMID, NULL);
        msg_qid = -1;
    }
    sem_destroy(&wrt);
    sem_destroy(&readers_sem);
    sem_destroy(&writers_sem);
    pthread_mutex_destroy(&adm.mutex);
    pthread_cond_destroy(&adm.ok_priority);
    pthread_cond_destroy(&adm.ok_normal);
}

static void print_stats(void)
{
    printf("\n[stats] richieste servite: %d | lettori attivi: %d | scrittori attivi: %d\n"
           "[stats] ammissione -> prioritari(in/att): %d/%d  ordinari(in/att): %d/%d\n",
           served_requests, active_readers, active_writers,
           adm.in_priority, adm.wait_priority, adm.in_normal, adm.wait_normal);
    fflush(stdout);
}

/* I gestori restano minimali: impostano flag/azzerano lo stato. La stampa e la
 * ricarica del DB (non async-signal-safe) sono differite al ciclo principale. */
static void handle_term(int signo) { (void) signo; running = 0; }
static void handle_usr1(int signo) { (void) signo; want_stats = 1; }
static void handle_usr2(int signo) { (void) signo; want_reload = 1; }
static void handle_alrm(int signo) { (void) signo; want_summary = 1; alarm(10); }

static void handle_chld(int signo)  /* rete di sicurezza anti-zombie */
{
    (void) signo;
    int saved = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0) { }
    errno = saved;
}

static void install_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);

    /* Niente SA_RESTART: msgrcv/fgets si interrompono (EINTR) e il ciclo
     * principale puo' esaminare i flag e uscire in modo pulito. */
    sa.sa_flags = 0;
    sa.sa_handler = handle_term; sigaction(SIGINT,  &sa, NULL);
                                 sigaction(SIGTERM, &sa, NULL);
    sa.sa_handler = handle_usr1; sigaction(SIGUSR1, &sa, NULL);
    sa.sa_handler = handle_usr2; sigaction(SIGUSR2, &sa, NULL);
    sa.sa_handler = handle_alrm; sigaction(SIGALRM, &sa, NULL);

    /* SIGCHLD con SA_RESTART: la raccolta dei figli non deve disturbare il ciclo */
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sa.sa_handler = handle_chld; sigaction(SIGCHLD, &sa, NULL);
}

/* Esegue le azioni differite richieste dai segnali (chiamata dal main). */
static void process_pending_signals(void)
{
    if (want_stats)  { want_stats = 0;  print_stats(); }
    if (want_summary){ want_summary = 0;
        printf("\n[riepilogo periodico] studenti=%d richieste=%d\n", n_students, served_requests);
        fflush(stdout);
    }
    if (want_reload) { want_reload = 0;
        write_request();                 /* ricarica in mutua esclusione        */
        n_students = load(db_filename, db);
        write_end();
        printf("\n[reload] database ricaricato: %d studenti.\n", n_students);
        fflush(stdout);
    }
}

/* ===========================================================================
 *  DISPATCHER DEI COMANDI (ciclo di test su stdin, Fasi 1-4)
 * ==========================================================================*/
static void handle_command_line(char *line)
{
    char cmd[32] = {0};
    if (sscanf(line, "%31s", cmd) != 1)
        return;                       /* riga vuota */

    served_requests++;

    if (strcmp(cmd, "count") == 0) {
        read_request();
        printf("Studenti caricati: %d\n", op_count());
        read_end();

    } else if (strcmp(cmd, "search") == 0) {
        char kind[16] = {0};
        sscanf(line, "%*s %15s", kind);
        if (strcmp(kind, "id") == 0) {
            int id = 0; sscanf(line, "%*s %*s %d", &id);
            student s;
            read_request();
            int ok = op_search_id(id, &s);
            read_end();
            if (ok)
                printf("Trovato: id=%d %s %s voto=%d\n", s.id, s.surname, s.name, s.exam_score);
            else
                printf("Nessuno studente con id=%d\n", id);
        } else if (strcmp(kind, "surname") == 0) {
            char sn[SURNAME_LEN] = {0}; sscanf(line, "%*s %*s %31s", sn);
            student s;
            read_request();
            int m = op_search_surname(sn, &s);
            read_end();
            printf("Corrispondenze per cognome '%s': %d\n", sn, m);
        } else {
            printf("Uso: search id <ID> | search surname <cognome>\n");
        }

    } else if (strcmp(cmd, "scan") == 0) {
        int val = -1, nproc = SCAN_NPROC; char mode[8] = "shm";
        int got = sscanf(line, "%*s %d %d %7s", &val, &nproc, mode);
        if (got < 1) { printf("Uso: scan <voto> [nproc] [pipe|shm]\n"); return; }
        int use_pipe = (got >= 3 && strcmp(mode, "pipe") == 0);
        long occ = op_scan(val, nproc, use_pipe);
        printf("Occorrenze del voto %d (%s, nproc=%d): %ld\n",
               val, use_pipe ? "pipe" : "shm", nproc, occ);

    } else if (strcmp(cmd, "insert") == 0) {
        student s; memset(&s, 0, sizeof(s));
        if (sscanf(line, "%*s %d %31s %31s %d", &s.id, s.surname, s.name, &s.exam_score) == 4) {
            write_request();
            int ok = op_insert(s);
            write_end();
            printf(ok ? "Inserito.\n" : "Inserimento fallito.\n");
        } else {
            printf("Uso: insert <ID> <cognome> <nome> <voto>\n");
        }

    } else if (strcmp(cmd, "update") == 0) {
        student s; memset(&s, 0, sizeof(s));
        if (sscanf(line, "%*s %d %31s %31s %d", &s.id, s.surname, s.name, &s.exam_score) == 4) {
            write_request();
            int ok = op_update(s);
            write_end();
            printf(ok ? "Aggiornato.\n" : "ID non trovato.\n");
        } else {
            printf("Uso: update <ID> <cognome> <nome> <voto>\n");
        }

    } else if (strcmp(cmd, "stats") == 0) {
        print_stats();

    } else if (strcmp(cmd, "load") == 0) {
        write_request();
        n_students = load(db_filename, db);
        write_end();
        printf("Ricaricati %d studenti.\n", n_students);

    } else if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "shutdown") == 0) {
        running = 0;

    } else {
        printf("Comando non riconosciuto: '%s'\n", cmd);
    }
}

static void server_loop_stdin(void)
{
    char line[256];
    printf("solabd pronto (ciclo di test stdin). Digita un comando ('quit' per uscire).\n");
    while (running) {
        process_pending_signals();
        if (!fgets(line, sizeof(line), stdin)) {
            if (errno == EINTR) { clearerr(stdin); continue; }
            break;                       /* EOF                                 */
        }
        handle_command_line(line);
    }
}

/* ===========================================================================
 *  FASE 5 -- CICLO BASATO SU CODA DI MESSAGGI
 * ==========================================================================*/

/* Invia la risposta al client (type = client_pid) dopo aver elaborato r. */
static void serve_and_reply(request *r)
{
    process_request(r);
    if (r->req_kind == REQ_QUIT) return;     /* il client non attende risposta  */
    r->type = r->client_pid;
    if (msgsnd(msg_qid, r, MSG_SIZE, 0) < 0) {
        if (errno != EIDRM && errno != EINVAL) perror("msgsnd reply");
    }
}

/* Crea/apre la coda gestendo il caso di coda gia' esistente (stale). */
static int open_message_queue(void)
{
    int qid = msgget(KEY, IPC_CREAT | IPC_EXCL | 0666);
    if (qid < 0 && errno == EEXIST) {
        int old = msgget(KEY, 0666);     /* coda lasciata da un'esecuzione precedente */
        if (old >= 0) msgctl(old, IPC_RMID, NULL);
        qid = msgget(KEY, IPC_CREAT | IPC_EXCL | 0666);
    }
    return qid;
}

static void server_loop_ipc(void)
{
    msg_qid = open_message_queue();
    if (msg_qid < 0) { perror("msgget"); running = 0; return; }
    printf("Server in ascolto sulla coda di messaggi (key=0x%lx, qid=%d, modo=%s, rw=%s).\n",
           (unsigned long) KEY, msg_qid,
           opt_use_pool ? "thread-pool" : "thread-per-request",
           opt_fair_rw ? "fair" : "base");
    fflush(stdout);

    while (running) {
        process_pending_signals();
        request r;
        ssize_t n = msgrcv(msg_qid, &r, MSG_SIZE, 1, 0);  /* solo richieste type==1 */
        if (n < 0) {
            if (errno == EINTR) continue;          /* segnale gestito: ricontrolla */
            if (errno == EIDRM || errno == EINVAL) break; /* coda rimossa          */
            perror("msgrcv"); break;
        }
        served_requests++;
        if (opt_use_pool) {
            enqueue(r);                            /* Fase 3b: al thread pool       */
        } else {
            request *rp = malloc(sizeof(*rp));      /* Fase 3a: un thread a richiesta*/
            if (!rp) { serve_and_reply(&r); continue; }
            *rp = r;
            pthread_t t;
            if (pthread_create(&t, NULL, worker_request, rp) != 0) {
                perror("pthread_create"); serve_and_reply(rp); free(rp); continue;
            }
            pthread_detach(t);
        }
    }
}

/* ===========================================================================
 *  MAIN
 * ==========================================================================*/
int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <file_db> [--stdin] [--thread-per] [--base-rw]\n", argv[0]);
        return EXIT_FAILURE;
    }
    db_filename = argv[1];

    int use_stdin = 0;
    for (int i = 2; i < argc; i++) {
        if      (strcmp(argv[i], "--stdin") == 0)      use_stdin = 1;
        else if (strcmp(argv[i], "--thread-per") == 0) opt_use_pool = 0;
        else if (strcmp(argv[i], "--base-rw") == 0)    opt_fair_rw  = 0;
        else { fprintf(stderr, "Opzione sconosciuta: %s\n", argv[i]); return EXIT_FAILURE; }
    }

    /* FASE 1: caricamento del database. */
    n_students = load(db_filename, db);
    printf("Server avviato: %d studenti caricati da '%s'.\n", n_students, db_filename);

    /* Insieme dei segnali gestiti dal solo thread main: bloccato nei worker. */
    sigemptyset(&g_handled_set);
    sigaddset(&g_handled_set, SIGINT);
    sigaddset(&g_handled_set, SIGTERM);
    sigaddset(&g_handled_set, SIGUSR1);
    sigaddset(&g_handled_set, SIGUSR2);
    sigaddset(&g_handled_set, SIGALRM);
    sigaddset(&g_handled_set, SIGCHLD);

    sync_init();          /* Fase 4 : mutex, semafori                          */
    admission_init();     /* Fase 6 : mutex e condition variables              */
    install_handlers();   /* Fase 7 : gestori di segnale                       */

    /* Blocca i segnali gestiti, poi crea il pool (i worker ereditano il blocco),
     * infine sblocca i segnali nel solo thread main. */
    pthread_sigmask(SIG_BLOCK, &g_handled_set, NULL);
    if (opt_use_pool && !use_stdin) {
        if (pool_init() != 0) { cleanup_resources(); return EXIT_FAILURE; }  /* Fase 3b */
    }
    pthread_sigmask(SIG_UNBLOCK, &g_handled_set, NULL);

    alarm(10);            /* Fase 7 : primo riepilogo periodico                */

    if (use_stdin) {
        server_loop_stdin();          /* Fasi 1-4: prova manuale dei comandi    */
    } else {
        server_loop_ipc();            /* Fase 5: server completo su coda        */
    }

    cleanup_resources();  /* Fase 7: rilascio risorse (no code/zombie orfani)   */
    printf("Server terminato.\n");
    return EXIT_SUCCESS;
}
