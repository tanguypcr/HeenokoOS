#define _POSIX_C_SOURCE 200809L

#include "disk.h"

/* <errno.h> : donne accès à errno, variable dans laquelle un appel système qui échoue range
   le code de l'erreur (ENOENT, EINTR...). perror() affiche ce code, mais errno permet en plus
   de réagir différemment selon la cause de l'échec */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

/* static devant une variable globale : la variable n'est visible que dans ce fichier .c */
static int fd_disque = -1;

int disk_ouvrir(const char *chemin)
{
    /* struct stat / fstat() : fstat remplit cette structure avec les informations d'un fichier
       ouvert (taille, type, droits...). On s'en sert pour lire sa taille (champ st_size) */
    struct stat st;
    int nouveau = 0;

    /* O_RDWR : ouverture en lecture ET écriture */
    fd_disque = open(chemin, O_RDWR);
    if (fd_disque < 0) {
        /* ENOENT : code errno « fichier inexistant ». Seul ce cas est normal (premier
           lancement) : on crée alors le disque. Toute autre erreur est fatale */
        if (errno != ENOENT) {
            perror(chemin);
            return -1;
        }
        /* O_CREAT + troisième argument : crée le fichier s'il n'existe pas, avec les droits
           0644 */
        fd_disque = open(chemin, O_RDWR | O_CREAT, 0644);
        if (fd_disque < 0) {
            perror(chemin);
            return -1;
        }
        nouveau = 1;
    }

    if (fstat(fd_disque, &st) < 0) {
        perror("fstat");
        return -1;
    }
    if (st.st_size != TAILLE_DISQUE) {
        /* ftruncate() : fixe la taille d'un fichier ouvert. On le vide (taille 0) puis on
           l'agrandit à 1 Mo, la partie ajoutée est remplie de zéros */
        if (ftruncate(fd_disque, 0) < 0 || ftruncate(fd_disque, TAILLE_DISQUE) < 0) {
            perror("ftruncate");
            return -1;
        }
        nouveau = 1;
    }
    return nouveau;
}

void disk_fermer(void)
{
    if (fd_disque >= 0) {
        /* fsync() : force l'écriture physique sur le disque des données encore en mémoire
           dans le cache du noyau Linux, pour que data.bin soit à jour à la fermeture */
        fsync(fd_disque);
        close(fd_disque);
        fd_disque = -1;
    }
}

int lire_bloc(uint32_t num, void *tampon)
{
    if (num >= NB_BLOCS)
        return -1;
    /* pread() : comme read(), mais lit à la position indiquée (en octets) dans le fichier,
       sans avoir à déplacer le curseur au préalable */
    if (pread(fd_disque, tampon, TAILLE_BLOC, (off_t)num * TAILLE_BLOC) != TAILLE_BLOC)
        return -1;
    return 0;
}

int ecrire_bloc(uint32_t num, const void *tampon)
{
    if (num >= NB_BLOCS)
        return -1;
    /* pwrite() : équivalent de pread() pour l'écriture, à une position donnée */
    if (pwrite(fd_disque, tampon, TAILLE_BLOC, (off_t)num * TAILLE_BLOC) != TAILLE_BLOC)
        return -1;
    return 0;
}
