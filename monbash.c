/*
 * monbash.c — Shell interactif de HeenokoOS.
 *
 * Crée les tubes anonymes, lance le noyau avec fork()/execvp(), puis lit les
 * commandes de l'utilisateur et transmet les requêtes au noyau.
 *     monbash [disque]        (par défaut : data.bin)
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"
#include "parser.h"

static int ecrire_complet(int fd, const void *tampon, size_t n)
{
    const char *p = tampon;

    while (n > 0) {
        ssize_t r = write(fd, p, n);

        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

static int lire_complet(int fd, void *tampon, size_t n)
{
    char *p = tampon;

    while (n > 0) {
        ssize_t r = read(fd, p, n);

        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

/* Le noyau est cherché à côté de l'exécutable monbash. */
static void chemin_noyau(char *sortie, size_t taille)
{
    char    exe[TAILLE_CHEMIN];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);

    if (n > 0) {
        exe[n] = '\0';
        snprintf(sortie, taille, "%s/noyau", dirname(exe));
    } else {
        snprintf(sortie, taille, "./noyau");
    }
}

static pid_t lancer_noyau(const char *disque, int *fd_requetes, int *fd_reponses)
{
    int   tube_req[2], tube_rep[2];
    pid_t pid;

    if (pipe(tube_req) < 0 || pipe(tube_rep) < 0) {
        perror("pipe");
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        char  noyau[2 * TAILLE_CHEMIN], arg_req[16], arg_rep[16];
        char *args[5];

        close(tube_req[1]);
        close(tube_rep[0]);
        chemin_noyau(noyau, sizeof noyau);
        snprintf(arg_req, sizeof arg_req, "%d", tube_req[0]);
        snprintf(arg_rep, sizeof arg_rep, "%d", tube_rep[1]);
        args[0] = noyau;
        args[1] = arg_req;
        args[2] = arg_rep;
        args[3] = (char *)disque;
        args[4] = NULL;
        execvp(noyau, args);
        perror(noyau);
        _exit(127);
    }

    close(tube_req[0]);
    close(tube_rep[1]);
    *fd_requetes = tube_req[1];
    *fd_reponses = tube_rep[0];
    return pid;
}

int main(int argc, char *argv[])
{
    const char    *disque = (argc > 1) ? argv[1] : DISQUE_DEFAUT;
    char           ligne[TAILLE_LIGNE + 1];
    char           msg[256];
    char           cwd[TAILLE_CHEMIN] = "/";
    struct requete req;
    struct reponse rep;
    int            fd_requetes, fd_reponses, interactif;
    pid_t          pid;

    signal(SIGINT, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    interactif = isatty(STDIN_FILENO);

    pid = lancer_noyau(disque, &fd_requetes, &fd_reponses);
    if (pid < 0)
        return EXIT_FAILURE;

    /* Le noyau confirme le montage du disque avant la première invite. */
    if (lire_complet(fd_reponses, &rep, sizeof rep) < 0 || rep.statut < 0) {
        fprintf(stderr, "monbash : le noyau n'a pas pu démarrer.\n");
        waitpid(pid, NULL, 0);
        return EXIT_FAILURE;
    }

    if (interactif)
        printf("HeenokoOS — monbash. Tapez help pour la liste des commandes.\n");

    for (;;) {
        if (interactif) {
            printf("\033[1;33mheenoko:%s$\033[0m ", cwd);
            fflush(stdout);
        }
        if (!fgets(ligne, sizeof ligne, stdin)) {
            if (interactif)
                printf("\n");
            break;
        }
        if (!strchr(ligne, '\n') && !feof(stdin)) {
            int c;

            while ((c = getchar()) != '\n' && c != EOF)
                ;
            fprintf(stderr, "monbash : ligne trop longue (%d caractères max)\n", TAILLE_LIGNE - 1);
            continue;
        }

        switch (parser_analyser(ligne, &req, msg, sizeof msg)) {
        case ANALYSE_VIDE:
            continue;
        case ANALYSE_ERREUR:
            fprintf(stderr, "monbash : %s\n", msg);
            continue;
        case ANALYSE_AIDE:
            parser_aide();
            continue;
        case ANALYSE_QUITTER:
            goto fin;
        case ANALYSE_REQUETE:
            break;
        }

        if (ecrire_complet(fd_requetes, &req, sizeof req) < 0 ||
            lire_complet(fd_reponses, &rep, sizeof rep) < 0) {
            fprintf(stderr, "monbash : le noyau ne répond plus.\n");
            goto attente;
        }
        snprintf(cwd, sizeof cwd, "%s", rep.cwd);
    }

fin:
    memset(&req, 0, sizeof req);
    req.op = OP_QUITTER;
    snprintf(req.ligne, sizeof req.ligne, "exit");
    if (ecrire_complet(fd_requetes, &req, sizeof req) == 0)
        lire_complet(fd_reponses, &rep, sizeof rep);
attente:
    close(fd_requetes);
    close(fd_reponses);
    waitpid(pid, NULL, 0);
    return EXIT_SUCCESS;
}
