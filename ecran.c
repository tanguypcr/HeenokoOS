/*
 * ecran.c — Processus Écran de HeenokoOS.
 *
 * Terminal passif, à lancer en premier : crée la FIFO /tmp/heenok_screen_fifo
 * et affiche tout ce que le noyau y écrit. Après la fin d'une session, il
 * attend la suivante. Ctrl-C pour l'éteindre.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"

static volatile sig_atomic_t eteindre = 0;

static void sur_signal(int sig)
{
    (void)sig;
    eteindre = 1;
}

static void banniere(void)
{
    printf("\033[1;33m"
           "  _   _                       _              ___  ____\n"
           " | | | | ___  ___ _ __   ___ | | _____      / _ \\/ ___|\n"
           " | |_| |/ _ \\/ _ \\ '_ \\ / _ \\| |/ / _ \\    | | | \\___ \\\n"
           " |  _  |  __/  __/ | | | (_) |   < (_) |   | |_| |___) |\n"
           " |_| |_|\\___|\\___|_| |_|\\___/|_|\\_\\___/     \\___/|____/\n"
           "\033[0m"
           "                      Écran — NSY103\n\n");
}

int main(void)
{
    struct sigaction sa;
    struct stat      st;
    char             tampon[4096];

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = sur_signal;
    sigemptyset(&sa.sa_mask);
    /* Pas de SA_RESTART : open() et read() bloquants doivent être interrompus. */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (mkfifo(FIFO_ECRAN, 0666) < 0 && errno != EEXIST) {
        perror(FIFO_ECRAN);
        return EXIT_FAILURE;
    }
    if (stat(FIFO_ECRAN, &st) < 0 || !S_ISFIFO(st.st_mode)) {
        fprintf(stderr, "%s existe mais n'est pas une FIFO.\n", FIFO_ECRAN);
        return EXIT_FAILURE;
    }

    banniere();
    while (!eteindre) {
        int     fd;
        ssize_t n;

        printf("\033[2m[écran] En attente du noyau sur %s...\033[0m\n", FIFO_ECRAN);
        fflush(stdout);

        /* Bloque jusqu'à ce que le noyau ouvre la FIFO en écriture. */
        fd = open(FIFO_ECRAN, O_RDONLY);
        if (fd < 0) {
            if (errno == EINTR)
                continue;
            perror(FIFO_ECRAN);
            break;
        }
        while ((n = read(fd, tampon, sizeof tampon)) > 0)
            fwrite(tampon, 1, (size_t)n, stdout), fflush(stdout);
        close(fd);
        if (!eteindre)
            printf("\033[2m[écran] Session terminée.\033[0m\n\n");
    }

    unlink(FIFO_ECRAN);
    printf("\n[écran] Écran éteint.\n");
    return EXIT_SUCCESS;
}
