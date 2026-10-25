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

/* static : variable visible uniquement dans ce fichier.
   volatile sig_atomic_t : la variable est modifiée dans le gestionnaire de signal, donc
   « en dehors » du déroulement normal de main(). volatile oblige le compilateur à relire sa
   valeur à chaque test de la boucle (sans cela, il pourrait garder une copie en registre et
   ne jamais voir le changement). sig_atomic_t garantit que l'écriture se fait en une seule
   opération, sans pouvoir être coupée par un signal. C'est le seul type que la norme
   autorise à modifier sans risque depuis un gestionnaire. */
static volatile sig_atomic_t eteindre = 0;

static void sur_signal(int sig)
{
    /* (void)sig : le paramètre est imposé par le prototype du gestionnaire mais n'est pas
       utilisé. Cette conversion indique que c'est volontaire et supprime l'avertissement
       « paramètre inutilisé » */
    (void)sig;
    eteindre = 1;
}

static void banniere(void)
{
    /* \033[...m : séquences d'échappement ANSI (\033 = caractère ESC) interprétées par le
       terminal : 1;33 = jaune gras, 2 = texte atténué, 0 = retour à l'affichage normal.
       Des chaînes écrites côte à côte sont concaténées par le compilateur en une seule */
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
    /* struct stat : structure remplie par stat() avec les informations d'un fichier
       (type, taille, droits...) */
    struct stat      st;
    char             tampon[4096];

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = sur_signal;
    /* sigemptyset() : initialise sa_mask à l'ensemble vide, c'est-à-dire qu'aucun signal
       supplémentaire n'est bloqué pendant l'exécution du gestionnaire. sa_flags reste à 0
       (memset) : sans SA_RESTART, un open() ou un read() bloqué est interrompu par Ctrl-C
       et renvoie -1 avec errno == EINTR, ce qui permet de sortir de la boucle */
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* EEXIST : code errno « le fichier existe déjà ». Si la FIFO a été laissée par une
       exécution précédente, mkfifo() échoue avec ce code : ce n'est pas une erreur et on
       réutilise la FIFO existante */
    if (mkfifo(FIFO_ECRAN, 0666) < 0 && errno != EEXIST) {
        perror(FIFO_ECRAN);
        return EXIT_FAILURE;
    }
    /* stat() / S_ISFIFO() : stat() lit les informations du fichier à partir de son chemin ;
       la macro S_ISFIFO(st.st_mode) est vraie si c'est bien un tube nommé. On vérifie ainsi
       qu'un fichier ordinaire portant ce nom n'occupe pas la place de la FIFO */
    if (stat(FIFO_ECRAN, &st) < 0 || !S_ISFIFO(st.st_mode)) {
        /* stderr : flux de sortie d'erreur, séparé de la sortie normale (stdout) */
        fprintf(stderr, "%s existe mais n'est pas une FIFO.\n", FIFO_ECRAN);
        return EXIT_FAILURE;
    }

    banniere();
    while (!eteindre) {
        int     fd;
        ssize_t n;

        printf("\033[2m[écran] En attente du noyau sur %s...\033[0m\n", FIFO_ECRAN);
        fflush(stdout);

        fd = open(FIFO_ECRAN, O_RDONLY);
        if (fd < 0) {
            /* EINTR : open() a été interrompu par un signal (Ctrl-C). On revient au test de
               la boucle, qui voit eteindre == 1 et s'arrête */
            if (errno == EINTR)
                continue;
            perror(FIFO_ECRAN);
            break;
        }
        /* fwrite() : écrit n octets bruts sur un flux (ici stdout). Contrairement à
           printf("%s"), il n'a pas besoin d'un '\0' final : les données lues dans la FIFO
           n'en contiennent pas */
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
