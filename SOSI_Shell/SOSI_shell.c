/* ************************************************************************** *
 *  SOSI Shell - Shell Unix-like semplificata in C
 *  Corso di Sistemi Operativi e Sicurezza Informatica
 *  Prof. Francesco Fontanella
 *
 *  Implementazione incrementale a partire dallo skeleton SOSI_shell.c.
 *  Funzionalita' realizzate:
 *    Fase 0: pulizia dello skeleton, allineamento di prototipi e firme,
 *            dichiarazione delle variabili mancanti, compilazione pulita
 *            con -Wall -Wextra.
 *    Fase 1: prompt informativo (user@host:cwd$), parsing della riga,
 *            built-in exit/quit/cd/history, ricerca dell'eseguibile in PATH
 *            (con supporto ai path assoluti), esecuzione con fork/execv/waitpid,
 *            foreground/background ('&') e redirezione dell'output ('>').
 *    Fase 2: cronologia degli ultimi 10 comandi su array circolare, comando
 *            built-in history, recupero con '!!', '!N' e (bonus) '!str'.
 *    Fase 3: pipeline semplice tra due comandi esterni con pipe/fork/dup2/close.
 *
 *  Modifiche preliminari (Fase 0) rispetto allo skeleton originale:
 *   - main(): dichiarata la variabile di ciclo 'i' usata nell'inizializzazione
 *             della history (era usata ma mai dichiarata).
 *   - built_ins(): la firma e' stata allineata al prototipo e all'unica
 *             chiamata presente nel main, ossia int built_ins(char *p[]).
 *   - check_file(): aggiunto il parametro 'exe' anche nel prototipo, per
 *             allinearlo alla definizione int check_file(char *path, int exe).
 *   - get_history(): trasformata in funzione che restituisce un esito (int)
 *             per distinguere recupero riuscito/fallito.
 *   - get_string(): gestione robusta dell'EOF (Ctrl-D) e del newline finale.
 *   - rimosse le macro inc()/dec() dello skeleton, che generavano comportamento
 *             indefinito (uso di val++ in un'espressione poi riassegnata), in
 *             favore di aritmetica modulare esplicita e leggibile.
 * ************************************************************************** */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define FALSE 0
#define TRUE  1
#define MAX_STRING 1024
#define MAX_ARGS   1000
#define H_SIZE     10

/* ---- prototipi ---- */
void get_string(char *str, int size);
void concat(const char *in_path, char *com, char *out_path);

void prompt(void);
int  read_command(char *p[]);

/* funzioni della history */
void manage_history(char *c);
int  get_history(char *c);
void update_history(char *c);
void show_history(void);

int  built_ins(char *p[]);
void ch_dir(char *dir);
int  check_file(char *path, int exe);
int  find_path(char *com, char *path);
void redirStdout(char *filename);

/* esecuzione della pipeline (Fase 3) */
void run_pipeline(char *p1[], char *p2[], int foreground);

/* liberazione della memoria allocata per gli argomenti (registrata con atexit) */
void cleanup(void);

/* ---- array degli argomenti dei comandi (globali per poterli liberare) ---- */
char *pars[MAX_ARGS];       /* argomenti del comando (o del 1o lato pipe) */
char *pars2[MAX_ARGS];      /* argomenti del secondo comando (lato pipe)  */

/* ---- variabili globali per la redirezione dell'output ---- */
char outName[MAX_STRING];   /* nome del file di redirezione         */
int  outRe;                 /* TRUE se e' richiesta la redirezione  */

/* ---- variabili globali per la pipeline (Fase 3) ---- */
int   pipeRe;               /* TRUE se la riga contiene una pipe '|' */

/* ---- variabili globali per la history (Fase 2) ---- */
typedef struct Command {
    int  n;                 /* numero progressivo assoluto del comando */
    char com[MAX_STRING];   /* testo del comando                       */
} com;

struct Command history[H_SIZE];
int h_i;   /* indice della prossima posizione da scrivere (circolare) */
int h_n;   /* contatore progressivo assoluto dei comandi              */

/* ************************************************************************** *
 *                                 MAIN
 * ************************************************************************** */
int main(int argc, char *argv[])
{
    char path[MAX_STRING];
    int   w, pid, s, i;

    (void)argc;  /* parametri non usati: marcati per evitare warning */
    (void)argv;

    atexit(cleanup);   /* libera gli array degli argomenti alla terminazione */

    /* inizializzazione delle variabili della history */
    h_i = 0;
    h_n = 0;
    for (i = 0; i < H_SIZE; ++i) {
        history[i].com[0] = '\0';
        history[i].n = 0;
    }
    /* l'array dei puntatori agli argomenti parte azzerato cosi' read_command
       sa quali celle deve ancora allocare con malloc() */
    for (i = 0; i < MAX_ARGS; ++i) {
        pars[i]  = NULL;
        pars2[i] = NULL;
    }

    while (TRUE) {
        prompt();
        w = read_command(pars);   /* w == TRUE => foreground */

        /* Fase 3: la pipe ha una gestione completamente diversa e va valutata
           per prima, cosi' anche "| cmd" (pipe iniziale) viene riconosciuta
           come errore di sintassi anziche' scambiata per riga vuota */
        if (pipeRe) {
            run_pipeline(pars, pars2, w);
            continue;
        }

        /* riga vuota (anche dopo un recupero history fallito) o built-in:
           si torna al prompt senza creare processi */
        if (pars[0] == NULL || strlen(pars[0]) == 0 || built_ins(pars))
            continue;

        /* ricerca dell'eseguibile nei percorsi di PATH (o path assoluto) */
        if (!find_path(pars[0], path)) {
            printf("ERROR: command not found: %s\n", pars[0]);
            continue;
        }

        pid = fork();
        if (pid < 0) {                 /* fork fallita */
            perror("fork");
            continue;
        }

        if (pid != 0) {                /* PADRE */
            if (w)                     /* foreground: attende il figlio */
                waitpid(pid, &s, 0);
            /* background: non attende, torna subito al prompt */
        } else {                       /* FIGLIO */
            if (outRe)                 /* eventuale redirezione dell'output */
                redirStdout(outName);

            execv(path, pars);
            /* si arriva qui solo se execv fallisce */
            perror("execv");
            exit(1);
        }
    }

    return 0;
}

/* ************************************************************************** *
 *  prompt()
 *  Stampa una stringa informativa nella forma  user@host:cwd$
 *  usando le variabili d'ambiente USER/HOSTNAME e la working dir corrente.
 * ************************************************************************** */
void prompt(void)
{
    char *user = getenv("USER");
    char  host[MAX_STRING];
    char  cwd[MAX_STRING];
    char *home = getenv("HOME");

    if (user == NULL)
        user = "user";

    /* HOSTNAME spesso non e' esportato nell'ambiente: si usa gethostname() */
    if (gethostname(host, sizeof(host)) != 0)
        strcpy(host, "host");

    /* directory corrente: getcwd() e' sempre aggiornata, a differenza di $PWD */
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        strcpy(cwd, "?");

    /* abbreviazione della home directory con '~', come nelle shell reali */
    if (home != NULL && strncmp(cwd, home, strlen(home)) == 0) {
        printf("%s@%s:~%s$ ", user, host, cwd + strlen(home));
    } else {
        printf("%s@%s:%s$ ", user, host, cwd);
    }
    fflush(stdout);
}

/* ************************************************************************** *
 *  read_command()
 *  Legge una riga da stdin, la registra nella history, la suddivide in token
 *  e prepara l'array p[] nel formato richiesto da execv (terminato da NULL).
 *  Durante la scansione riconosce e rimuove i metacaratteri della shell:
 *     '>' redirezione output  -> imposta outRe / outName
 *     '&' background          -> la funzione restituisce FALSE
 *     '|' pipe                -> imposta pipeRe e instrada i token successivi
 *                                nel secondo array di argomenti (pars2)
 *  Valore di ritorno: TRUE se in foreground, FALSE se in background.
 * ************************************************************************** */
int read_command(char *p[])
{
    char tmp[MAX_STRING], *s;
    int  fg, k;
    char **cur;     /* array di destinazione corrente: p oppure pars2 */
    int   ci;       /* indice nell'array corrente                     */

    /* reset dello stato per ogni nuovo comando */
    outRe  = FALSE;
    pipeRe = FALSE;
    fg     = TRUE;

    /* libero le allocazioni del comando precedente e riparto da array puliti:
       evita memory leak quando un comando e' piu' corto del precedente e il
       terminatore NULL sovrascriverebbe un puntatore gia' allocato. Le celle
       sono allocate sempre in modo contiguo da 0 fino al primo NULL. */
    for (k = 0; k < MAX_ARGS && p[k] != NULL; ++k) {
        free(p[k]);
        p[k] = NULL;
    }
    for (k = 0; k < MAX_ARGS && pars2[k] != NULL; ++k) {
        free(pars2[k]);
        pars2[k] = NULL;
    }

    get_string(tmp, MAX_STRING);

    /* l'utente ha premuto solo INVIO: restituisco la stringa vuota */
    if (strlen(tmp) == 0) {
        if (p[0] == NULL)
            p[0] = (char *) malloc(MAX_STRING);
        p[0][0] = '\0';
        return TRUE;
    }

    /* gestione della history: deve avvenire PRIMA di strtok(), perche'
       strtok() modifica tmp inserendo '\0' al posto degli spazi.
       Per i comandi recuperati (!! / !N / !str) tmp viene riscritto con
       il comando recuperato; in caso di recupero fallito tmp diventa "" */
    manage_history(tmp);
    if (strlen(tmp) == 0) {     /* recupero fallito: si torna al prompt */
        if (p[0] == NULL)
            p[0] = (char *) malloc(MAX_STRING);
        p[0][0] = '\0';
        return TRUE;
    }

    /* tokenizzazione */
    cur = p;
    ci  = 0;
    s   = strtok(tmp, " ");

    while (s != NULL) {

        if (strcmp(s, "&") == 0) {            /* background */
            fg = FALSE;
            s = strtok(NULL, " ");
            continue;
        }

        if (strcmp(s, ">") == 0) {            /* redirezione output */
            s = strtok(NULL, " ");
            if (s != NULL) {
                strncpy(outName, s, MAX_STRING - 1);
                outName[MAX_STRING - 1] = '\0';
                outRe = TRUE;
            }
            s = strtok(NULL, " ");
            continue;
        }

        if (strcmp(s, "|") == 0) {            /* pipe: passo al 2o comando */
            cur[ci] = NULL;                   /* chiudo il 1o array        */
            cur = pars2;
            ci  = 0;
            pipeRe = TRUE;
            s = strtok(NULL, " ");
            continue;
        }

        /* token normale: lo copio nell'array corrente (allocando se serve) */
        if (cur[ci] == NULL)
            cur[ci] = (char *) malloc(MAX_STRING);
        strncpy(cur[ci], s, MAX_STRING - 1);
        cur[ci][MAX_STRING - 1] = '\0';
        ci++;

        s = strtok(NULL, " ");
    }

    cur[ci] = NULL;   /* l'array corrente deve terminare con NULL per execv */

    /* se non c'e' la pipe ma p e' rimasto vuoto, garantisco p[0] valido */
    if (cur == p && ci == 0) {
        if (p[0] == NULL)
            p[0] = (char *) malloc(MAX_STRING);
        p[0][0] = '\0';
    }

    return fg;
}

/* ************************************************************************** *
 *  manage_history()
 *  Punto d'ingresso della gestione history. Se il comando inizia con '!'
 *  prova a recuperarlo dalla cronologia; in caso di successo lo stampa.
 *  Il comando (eventualmente recuperato) viene poi registrato in cronologia.
 * ************************************************************************** */
void manage_history(char *c)
{
    if (c[0] == '!') {
        if (!get_history(c)) {   /* recupero fallito */
            c[0] = '\0';         /* segnalo riga "vuota" al chiamante */
            return;
        }
        printf("%s\n", c);       /* echo del comando recuperato */
        fflush(stdout);          /* garantisce l'ordine prima di fork/execv */
    }

    if (strlen(c) > 0)
        update_history(c);
}

/* ************************************************************************** *
 *  get_history()
 *  Gestisce i comandi che iniziano con '!':
 *     !!    -> comando piu' recente
 *     !N    -> comando con numero progressivo N
 *     !str  -> (bonus) ultimo comando che inizia con la stringa str
 *  In caso di successo copia il comando recuperato in c e restituisce TRUE.
 *  In caso di errore stampa un messaggio e restituisce FALSE.
 * ************************************************************************** */
int get_history(char *c)
{
    int n, k, idx, found = FALSE;

    /* cronologia vuota */
    if (h_n == 0) {
        printf("history: empty history\n");
        return FALSE;
    }

    /* caso "!!": comando piu' recente (ultima posizione scritta) */
    if (c[1] == '!' && c[2] == '\0') {
        idx = (h_i - 1 + H_SIZE) % H_SIZE;
        strncpy(c, history[idx].com, MAX_STRING - 1);
        c[MAX_STRING - 1] = '\0';
        return TRUE;
    }

    /* caso "!N": numero progressivo */
    if (sscanf(c + 1, "%d", &n) == 1) {
        /* scansione delle celle valide alla ricerca di .n == n */
        for (k = 0; k < H_SIZE && !found; ++k) {
            if (history[k].com[0] != '\0' && history[k].n == n) {
                strncpy(c, history[k].com, MAX_STRING - 1);
                c[MAX_STRING - 1] = '\0';
                found = TRUE;
            }
        }
        if (!found)
            printf("history: command !%d not found\n", n);
        return found;
    }

    /* caso "!str" (bonus): ultimo comando che inizia con str.
       Scansione all'indietro a partire dal piu' recente. */
    {
        char *str = c + 1;
        size_t len = strlen(str);
        if (len == 0) {
            printf("history: invalid history reference\n");
            return FALSE;
        }
        idx = (h_i - 1 + H_SIZE) % H_SIZE;
        for (k = 0; k < H_SIZE && !found; ++k) {
            if (history[idx].com[0] != '\0' &&
                strncmp(history[idx].com, str, len) == 0) {
                strncpy(c, history[idx].com, MAX_STRING - 1);
                c[MAX_STRING - 1] = '\0';
                found = TRUE;
            }
            idx = (idx - 1 + H_SIZE) % H_SIZE;
        }
        if (!found)
            printf("history: no command starting with \"%s\"\n", str);
        return found;
    }
}

/* ************************************************************************** *
 *  update_history()
 *  Inserisce il comando c nella prossima cella dell'array circolare,
 *  assegnandogli il numero progressivo assoluto h_n e avanzando l'indice h_i.
 * ************************************************************************** */
void update_history(char *c)
{
    h_n++;                                  /* nuovo numero progressivo */
    history[h_i].n = h_n;
    strncpy(history[h_i].com, c, MAX_STRING - 1);
    history[h_i].com[MAX_STRING - 1] = '\0';
    h_i = (h_i + 1) % H_SIZE;               /* avanzamento circolare    */
}

/* ************************************************************************** *
 *  show_history()
 *  Stampa la cronologia dal comando piu' recente al meno recente.
 * ************************************************************************** */
void show_history(void)
{
    int k, idx, count;

    count = (h_n < H_SIZE) ? h_n : H_SIZE;  /* quanti comandi sono validi */
    idx   = (h_i - 1 + H_SIZE) % H_SIZE;    /* parto dal piu' recente     */

    for (k = 0; k < count; ++k) {
        printf("%d %s\n", history[idx].n, history[idx].com);
        idx = (idx - 1 + H_SIZE) % H_SIZE;
    }
}

/* ************************************************************************** *
 *  built_ins()
 *  Riconosce ed esegue i comandi built-in (exit, quit, cd, history).
 *  Restituisce TRUE se ha gestito un built-in, FALSE altrimenti.
 *  I built-in sono eseguiti dalla shell stessa (non via fork/execv) perche'
 *  modificano lo stato interno del processo shell (es. la working directory).
 * ************************************************************************** */
int built_ins(char *p[])
{
    if (strcmp(p[0], "exit") == 0 || strcmp(p[0], "quit") == 0)
        exit(0);

    if (strcmp(p[0], "cd") == 0) {
        ch_dir(p[1]);          /* p[1] puo' essere NULL: cd verso la home */
        return TRUE;
    }

    if (strcmp(p[0], "history") == 0) {
        show_history();
        return TRUE;
    }

    return FALSE;
}

/* ************************************************************************** *
 *  ch_dir()
 *  Cambia la working directory:
 *    - cd senza argomenti     -> home directory ($HOME)
 *    - cd /percorso/assoluto  -> path assoluto
 *    - cd nome_relativo       -> sottodirectory della directory corrente
 *  In caso di errore stampa un messaggio ma non termina la shell.
 * ************************************************************************** */
void ch_dir(char *dir)
{
    char  cwd[MAX_STRING];
    char  target[MAX_STRING];
    char *home = getenv("HOME");

    /* cd senza argomenti: torno nella home */
    if (dir == NULL) {
        if (home == NULL) {
            printf("cd: HOME not set\n");
            return;
        }
        strncpy(target, home, MAX_STRING - 1);
        target[MAX_STRING - 1] = '\0';
    }
    else if (dir[0] == '/') {           /* path assoluto */
        strncpy(target, dir, MAX_STRING - 1);
        target[MAX_STRING - 1] = '\0';
    }
    else {                              /* path relativo: cwd + "/" + dir */
        if (getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("getcwd");
            return;
        }
        concat(cwd, dir, target);
    }

    /* verifico che la directory esista prima di provare a entrarci */
    if (!check_file(target, FALSE)) {
        printf("cd: %s: no such directory\n", dir ? dir : "");
        return;
    }

    if (chdir(target) != 0) {
        perror("cd");
        return;
    }

    /* aggiorno $PWD per coerenza con l'ambiente */
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        setenv("PWD", cwd, 1);
}

/* ************************************************************************** *
 *  check_file()
 *  Verifica, tramite stat(), che 'path' esista e sia:
 *    - un file regolare eseguibile, se exe == TRUE
 *    - una directory,               se exe == FALSE
 * ************************************************************************** */
int check_file(char *path, int exe)
{
    struct stat sb;

    if (stat(path, &sb) != 0)
        return FALSE;                       /* il path non esiste */

    if (exe) {
        /* file regolare con almeno un bit di esecuzione attivo */
        return S_ISREG(sb.st_mode) &&
               (sb.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH));
    } else {
        return S_ISDIR(sb.st_mode);
    }
}

/* ************************************************************************** *
 *  find_path()
 *  Trova il path completo dell'eseguibile 'com'. Se com e' gia' un path
 *  assoluto lo verifica direttamente; altrimenti scandisce la variabile
 *  d'ambiente PATH (separatore ':') usando strtok_r (rientrante).
 *  Restituisce TRUE e popola 'path' se l'eseguibile e' trovato.
 * ************************************************************************** */
int find_path(char *com, char *path)
{
    char  local_path[MAX_STRING];
    char *env_path, *token, *saveptr;

    /* caso 1: l'utente ha fornito direttamente un path assoluto */
    if (com[0] == '/') {
        if (check_file(com, TRUE)) {
            strncpy(path, com, MAX_STRING - 1);
            path[MAX_STRING - 1] = '\0';
            return TRUE;
        }
        return FALSE;
    }

    /* caso 2: scansione di PATH */
    env_path = getenv("PATH");
    if (env_path == NULL)
        return FALSE;

    strncpy(local_path, env_path, MAX_STRING - 1);
    local_path[MAX_STRING - 1] = '\0';

    token = strtok_r(local_path, ":", &saveptr);
    while (token != NULL) {
        concat(token, com, path);           /* token + "/" + com */
        if (check_file(path, TRUE))
            return TRUE;
        token = strtok_r(NULL, ":", &saveptr);
    }

    return FALSE;                            /* comando non trovato */
}

/* ************************************************************************** *
 *  concat()
 *  Costruisce un path concatenando in_path + "/" + com in out_path.
 * ************************************************************************** */
void concat(const char *in_path, char *com, char *out_path)
{
    strncpy(out_path, in_path, MAX_STRING - 1);
    out_path[MAX_STRING - 1] = '\0';
    strncat(out_path, "/", MAX_STRING - strlen(out_path) - 1);
    strncat(out_path, com, MAX_STRING - strlen(out_path) - 1);
}

/* ************************************************************************** *
 *  get_string()
 *  Legge una riga da stdin in str, rimuove il newline finale e gestisce
 *  l'EOF (Ctrl-D o fine di un file rediretto) terminando la shell in modo
 *  ordinato anziche' lasciare il buffer in stato indefinito.
 * ************************************************************************** */
void get_string(char *str, int size)
{
    size_t len;

    if (fgets(str, size, stdin) == NULL) {   /* EOF / errore di lettura */
        printf("\n");
        exit(0);
    }

    len = strnlen(str, size);
    if (len > 0 && str[len - 1] == '\n')      /* rimuovo il newline */
        str[len - 1] = '\0';
}

/* ************************************************************************** *
 *  redirStdout()
 *  Redirige stdout (e stderr, per rendere osservabili gli errori) sul file
 *  'filename', creandolo o troncandolo. Usa open() + dup2().
 *  Chiamata nel processo figlio, prima di execv().
 * ************************************************************************** */
void redirStdout(char *filename)
{
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        exit(1);
    }

    if (dup2(fd, STDOUT_FILENO) < 0 || dup2(fd, STDERR_FILENO) < 0) {
        perror("dup2");
        close(fd);
        exit(1);
    }

    close(fd);   /* il descrittore originale non serve piu' */
}

/* ************************************************************************** *
 *  run_pipeline()   [Fase 3]
 *  Esegue una pipeline semplice  cmd1 | cmd2  tra due comandi esterni.
 *  Crea una pipe e due processi figli: il primo redirige stdout sul lato di
 *  scrittura, il secondo redirige stdin sul lato di lettura. Il padre chiude
 *  entrambi i lati e attende i figli (in foreground). La chiusura corretta di
 *  tutti i descrittori e' indispensabile per evitare deadlock.
 * ************************************************************************** */
void run_pipeline(char *p1[], char *p2[], int foreground)
{
    char path1[MAX_STRING], path2[MAX_STRING];
    int  fd[2];
    pid_t pid1, pid2;
    int   s;

    /* controllo di sintassi: nessuno dei due comandi puo' essere vuoto */
    if (p1[0] == NULL || strlen(p1[0]) == 0) {
        printf("ERROR: syntax error near '|' (missing left command)\n");
        return;
    }
    if (p2[0] == NULL || strlen(p2[0]) == 0) {
        printf("ERROR: syntax error near '|' (missing right command)\n");
        return;
    }

    /* ricerca di entrambi gli eseguibili */
    if (!find_path(p1[0], path1)) {
        printf("ERROR: command not found: %s\n", p1[0]);
        return;
    }
    if (!find_path(p2[0], path2)) {
        printf("ERROR: command not found: %s\n", p2[0]);
        return;
    }

    if (pipe(fd) < 0) {
        perror("pipe");
        return;
    }

    /* --- primo figlio: scrive sul lato di scrittura della pipe --- */
    pid1 = fork();
    if (pid1 < 0) {
        perror("fork");
        close(fd[0]);
        close(fd[1]);
        return;
    }
    if (pid1 == 0) {
        dup2(fd[1], STDOUT_FILENO);   /* stdout -> pipe in scrittura */
        close(fd[0]);                 /* il lato di lettura non serve */
        close(fd[1]);                 /* dopo dup2 il fd originale e' superfluo */
        execv(path1, p1);
        perror("execv");
        exit(1);
    }

    /* --- secondo figlio: legge dal lato di lettura della pipe --- */
    pid2 = fork();
    if (pid2 < 0) {
        perror("fork");
        close(fd[0]);
        close(fd[1]);
        return;
    }
    if (pid2 == 0) {
        dup2(fd[0], STDIN_FILENO);    /* stdin <- pipe in lettura */
        close(fd[1]);                 /* il lato di scrittura non serve */
        close(fd[0]);
        if (outRe)                    /* eventuale redirezione: cmd2 > file */
            redirStdout(outName);
        execv(path2, p2);
        perror("execv");
        exit(1);
    }

    /* --- padre: chiude entrambi i lati e attende i figli --- */
    close(fd[0]);
    close(fd[1]);                     /* fondamentale: senza questo cmd2 non vede EOF */

    if (foreground) {
        waitpid(pid1, &s, 0);
        waitpid(pid2, &s, 0);
    }
}

/* ************************************************************************** *
 *  cleanup()
 *  Libera tutta la memoria allocata dinamicamente per gli argomenti dei
 *  comandi. Registrata con atexit(), viene invocata a ogni terminazione
 *  della shell (exit/quit, EOF), evitando memory leak segnalati dagli
 *  strumenti di analisi come valgrind.
 * ************************************************************************** */
void cleanup(void)
{
    int i;
    for (i = 0; i < MAX_ARGS; ++i) {
        free(pars[i]);   pars[i]  = NULL;
        free(pars2[i]);  pars2[i] = NULL;
    }
}
