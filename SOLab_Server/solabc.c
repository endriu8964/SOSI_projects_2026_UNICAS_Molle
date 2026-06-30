/* ============================================================================
 *  solabc.c  --  Client del progetto SOLab.
 *
 *  Progetto SOLab Server -- Corso di Sistemi Operativi
 *
 *  Schema "un server, molti client" (Fase 5):
 *    - il client costruisce una request con  type = 1  e  client_pid = getpid()
 *    - invia la richiesta con msgsnd(...)
 *    - attende la risposta con msgrcv(..., getpid(), ...)
 *  Il campo priority marca la classe del client (Fase 6) su ogni richiesta.
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include "solab.h"

static int qid = -1;          /* id della coda di messaggi */
static int my_priority = 0;   /* 0 = ordinario, 1 = prioritario (Fase 6) */

/* Invia una richiesta al server e ne attende la risposta sullo stesso canale.
 * Restituisce 0 se ok, -1 in caso di errore. */
static int send_request(request *r)
{
    r->type       = 1;             /* le richieste viaggiano con type = 1     */
    r->client_pid = getpid();      /* il server rispondera' con type = PID    */
    r->priority   = my_priority;   /* classe di ammissione (Fase 6)           */

    if (msgsnd(qid, r, MSG_SIZE, 0) < 0) { perror("msgsnd"); return -1; }

    /* attende SOLO il messaggio destinato a questo client (type == PID) */
    while (msgrcv(qid, r, MSG_SIZE, getpid(), 0) < 0) {
        if (errno == EINTR) continue;
        perror("msgrcv"); return -1;
    }
    return 0;
}

static void menu(void)
{
    printf("\n==== SOLab client (classe: %s) ====\n",
           my_priority ? "PRIORITARIO" : "ordinario");
    printf("1) Numero di studenti\n");
    printf("2) Cerca per ID\n");
    printf("3) Cerca per cognome\n");
    printf("4) Inserisci studente\n");
    printf("5) Aggiorna studente\n");
    printf("6) Scan: conta occorrenze di un voto\n");
    printf("7) Imposta/azzera priorita'\n");
    printf("0) Esci\n");
    printf("Scelta: ");
}

int main(void)
{
    qid = msgget(KEY, 0);          /* apre la coda creata dal server          */
    if (qid < 0) { perror("msgget (server attivo?)"); return EXIT_FAILURE; }

    int choice = -1;
    do {
        menu();
        if (scanf("%d", &choice) != 1) break;

        request r;
        memset(&r, 0, sizeof(r));

        switch (choice) {
        case 1:
            r.req_kind = REQ_COUNT;
            if (send_request(&r) == 0)
                printf("Studenti: %d\n", r.num);
            break;

        case 2:
            printf("ID: ");
            if (scanf("%d", &r.id) != 1) break;
            r.req_kind = REQ_SEARCH_ID;
            if (send_request(&r) == 0) {
                if (r.found)
                    printf("Trovato: id=%d %s %s voto=%d\n",
                           r.s.id, r.s.surname, r.s.name, r.s.exam_score);
                else
                    printf("Nessuno studente con id=%d\n", r.id);
            }
            break;

        case 3:
            printf("Cognome: ");
            if (scanf("%31s", r.surname) != 1) break;
            r.req_kind = REQ_SEARCH_SURNAME;
            if (send_request(&r) == 0) {
                printf("Corrispondenze: %d\n", r.num);
                if (r.found)
                    printf("Prima corrispondenza: id=%d %s %s voto=%d\n",
                           r.s.id, r.s.surname, r.s.name, r.s.exam_score);
            }
            break;

        case 4:
            printf("ID cognome nome voto: ");
            if (scanf("%d %31s %31s %d",
                      &r.s.id, r.s.surname, r.s.name, &r.s.exam_score) != 4) break;
            r.req_kind = REQ_INSERT;
            if (send_request(&r) == 0)
                printf(r.found ? "Inserito.\n" : "Inserimento fallito (DB pieno).\n");
            break;

        case 5:
            printf("ID cognome nome voto (nuovi valori): ");
            if (scanf("%d %31s %31s %d",
                      &r.s.id, r.s.surname, r.s.name, &r.s.exam_score) != 4) break;
            r.req_kind = REQ_UPDATE;
            if (send_request(&r) == 0)
                printf(r.found ? "Aggiornato.\n" : "ID non trovato.\n");
            break;

        case 6: {
            int mode = 0;
            printf("Voto da contare: ");
            if (scanf("%d", &r.id) != 1) break;
            printf("Numero di processi figli: ");
            if (scanf("%d", &r.scan_nproc) != 1) break;
            printf("Modalita' (0=shared memory, 1=pipe): ");
            if (scanf("%d", &mode) != 1) break;
            r.scan_pipe = (mode == 1) ? 1 : 0;
            r.req_kind = REQ_SCAN;
            if (send_request(&r) == 0)
                printf("Occorrenze del voto %d: %d\n", r.id, r.num);
            break;
        }

        case 7:
            my_priority = !my_priority;
            r.req_kind = REQ_PRIORITY;
            if (send_request(&r) == 0)
                printf("Ora sei un client %s.\n",
                       my_priority ? "PRIORITARIO" : "ordinario");
            break;

        case 0:
            printf("Uscita.\n");
            break;

        default:
            printf("Scelta non valida.\n");
        }
    } while (choice != 0);

    return EXIT_SUCCESS;
}
