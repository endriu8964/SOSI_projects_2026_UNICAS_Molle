/* ============================================================================
 *  solab.h  --  Definizioni comuni al server, al client e al generatore.
 *
 *  Progetto SOLab Server -- Corso di Sistemi Operativi
 *  Header condiviso da solabd (server), solabc (client) e gen_db (generatore).
 *  Rispetto allo scheletro fornito sono stati aggiunti SOLO due campi alla
 *  struct request (scan_nproc, scan_pipe) per veicolare i parametri del
 *  comando "scan" anche attraverso la coda di messaggi (Fase 5). La possibilita'
 *  di estendere l'header e' esplicitamente prevista dal commento originale.
 * ==========================================================================*/
#ifndef SOLAB_H
#define SOLAB_H

#include <sys/types.h>
#include <stddef.h>

/* ---- Dimensioni e costanti di configurazione ---------------------------- */
#define MAX_STUDENTS 100000   /* capacita' massima dell'array in memoria      */
#define POOL_SIZE        4    /* numero di thread worker del thread pool (F3) */
#define SCAN_NPROC       4    /* numero di processi figli di default (F2)     */
#define SERVER_CAPACITY  3    /* N: client serviti contemporaneamente (F6)    */

#define SURNAME_LEN     32
#define NAME_LEN        32
#define MAX_SCORE       31    /* voti 0..30; 31 = "30 e lode"                 */

/* ---- Chiave IPC condivisa (Fase 5) -------------------------------------- *
 * In alternativa alla costante si puo' usare ftok() su un percorso noto.    */
#define KEY        ((key_t) 18071968)

/* ---- Record dello studente ---------------------------------------------- *
 * Riusa la base dati delle prime esercitazioni di laboratorio.              */
typedef struct {
    int  id;
    char surname[SURNAME_LEN];
    char name[NAME_LEN];
    int  exam_score;          /* 0..MAX_SCORE                                */
} student;

/* ---- Tipi di richiesta -------------------------------------------------- */
enum req_kind {
    REQ_COUNT = 1,            /* numero di studenti          (lettore)       */
    REQ_SEARCH_ID,           /* ricerca per ID              (lettore)       */
    REQ_SEARCH_SURNAME,      /* ricerca per cognome         (lettore)       */
    REQ_SCAN,                /* conteggio occorrenze voto   (multiprocesso) */
    REQ_INSERT,              /* inserimento record          (scrittore)     */
    REQ_UPDATE,              /* aggiornamento record        (scrittore)     */
    REQ_PRIORITY,            /* imposta/azzera priorita' client (Fase 6)    */
    REQ_QUIT                 /* il client si disconnette                    */
};

/* ---- Messaggio scambiato tra client e server (Fase 5) ------------------- *
 * Il primo campo DEVE essere "long type" (mtype richiesto dalle code IPC).  *
 * Convenzione "un server, molti client":                                    *
 *   - il client invia con  type = 1                                         *
 *   - il server risponde con type = client_pid                              */
typedef struct {
    long    type;            /* mtype IPC (obbligatorio, NON e' contenuto)   */
    pid_t   client_pid;      /* PID del client: usato come type di risposta  */
    int     req_kind;        /* uno dei valori di enum req_kind              */
    int     priority;        /* 1 = client prioritario, 0 = ordinario (F6)   */
    int     id;              /* parametro intero (ID studente / valore voto) */
    char    surname[SURNAME_LEN]; /* parametro: cognome da cercare           */
    student s;               /* record di ritorno / record da inserire       */
    int     num;             /* contatore di ritorno (#studenti, #occorrenze)*/
    int     found;           /* esito: 1 = trovato/ok, 0 = non trovato/err   */
    int     scan_nproc;      /* Fase 2: numero di processi figli per scan    */
    int     scan_pipe;       /* Fase 2: 1 = pipe, 0 = shared memory          */
} request;

/* Dimensione utile del messaggio (esclude il campo "type"). */
#define MSG_SIZE (sizeof(request) - sizeof(long))

#endif /* SOLAB_H */
