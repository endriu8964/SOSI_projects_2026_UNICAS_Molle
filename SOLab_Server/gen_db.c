/* ============================================================================
 *  gen_db.c  --  Generatore del file binario di studenti.
 *
 *  Progetto SOLab Server -- Corso di Sistemi Operativi
 *  Questo file e' FORNITO COMPLETO (utility di supporto, analogo a gen_array.c).
 *
 *  Uso:   ./gen_db <file> <N> [seme]
 *      <file>  nome del file binario da creare
 *      <N>     numero di record student da generare
 *      [seme]  intero opzionale:
 *                - se presente: generazione con rand()/srand(seme) (riproducibile)
 *                - se assente : generazione con i byte di /dev/urandom
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "solab.h"

/* Alcuni cognomi e nomi di esempio per rendere i record leggibili. */
static const char *SURNAMES[] = {
    "Rossi","Bianchi","Verdi","Russo","Ferrari","Esposito","Romano","Colombo",
    "Ricci","Marino","Greco","Bruno","Gallo","Conti","De Luca","Costa"
};
static const char *NAMES[] = {
    "Luca","Marco","Giulia","Sara","Andrea","Anna","Paolo","Chiara",
    "Matteo","Elena","Davide","Martina","Simone","Laura","Roberto","Francesca"
};
#define N_SURN (int)(sizeof(SURNAMES)/sizeof(SURNAMES[0]))
#define N_NAME (int)(sizeof(NAMES)/sizeof(NAMES[0]))

/* Restituisce un intero pseudo-casuale.
 * use_urandom == 0 -> usa rand(); altrimenti legge da /dev/urandom (fd aperto). */
static unsigned int next_rand(int use_urandom, int urand_fd)
{
    if (!use_urandom)
        return (unsigned int) rand();

    unsigned int v = 0;
    if (read(urand_fd, &v, sizeof(v)) != (ssize_t) sizeof(v)) {
        perror("read /dev/urandom");
        exit(EXIT_FAILURE);
    }
    return v;
}

int main(int argc, char *argv[])
{
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Uso: %s <file> <N> [seme]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *filename = argv[1];
    long n = strtol(argv[2], NULL, 10);
    if (n < 0) { fprintf(stderr, "N deve essere >= 0\n"); return EXIT_FAILURE; }

    int use_urandom = (argc == 3);   /* nessun seme -> /dev/urandom */
    int urand_fd = -1;

    if (use_urandom) {
        urand_fd = open("/dev/urandom", O_RDONLY);
        if (urand_fd < 0) { perror("open /dev/urandom"); return EXIT_FAILURE; }
    } else {
        srand((unsigned int) strtol(argv[3], NULL, 10));
    }

    FILE *f = fopen(filename, "wb");
    if (!f) { perror("fopen"); return EXIT_FAILURE; }

    for (long i = 0; i < n; i++) {
        student s;
        memset(&s, 0, sizeof(s));
        s.id = (int) i + 1;                       /* ID univoci 1..N         */
        strncpy(s.surname, SURNAMES[next_rand(use_urandom, urand_fd) % N_SURN],
                SURNAME_LEN - 1);
        strncpy(s.name, NAMES[next_rand(use_urandom, urand_fd) % N_NAME],
                NAME_LEN - 1);
        s.exam_score = (int)(next_rand(use_urandom, urand_fd) % (MAX_SCORE + 1));

        if (fwrite(&s, sizeof(student), 1, f) != 1) {
            perror("fwrite");
            fclose(f);
            return EXIT_FAILURE;
        }
    }

    fclose(f);
    if (urand_fd >= 0) close(urand_fd);

    printf("Generati %ld record (%lu byte) in '%s'.\n",
           n, (unsigned long)(n * sizeof(student)), filename);
    return EXIT_SUCCESS;
}
