/*
 * disk.c — Lecture et écriture de blocs dans le fichier conteneur data.bin.
 */
#define _POSIX_C_SOURCE 200809L

#include "disk.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static int fd_disque = -1;

int disk_ouvrir(const char *chemin)
{
    struct stat st;
    int nouveau = 0;

    fd_disque = open(chemin, O_RDWR);
    if (fd_disque < 0) {
        if (errno != ENOENT) {
            perror(chemin);
            return -1;
        }
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
    /* Un fichier de mauvaise taille est considéré comme vierge. */
    if (st.st_size != TAILLE_DISQUE) {
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
        fsync(fd_disque);
        close(fd_disque);
        fd_disque = -1;
    }
}

int lire_bloc(uint32_t num, void *tampon)
{
    if (num >= NB_BLOCS)
        return -1;
    if (pread(fd_disque, tampon, TAILLE_BLOC, (off_t)num * TAILLE_BLOC) != TAILLE_BLOC)
        return -1;
    return 0;
}

int ecrire_bloc(uint32_t num, const void *tampon)
{
    if (num >= NB_BLOCS)
        return -1;
    if (pwrite(fd_disque, tampon, TAILLE_BLOC, (off_t)num * TAILLE_BLOC) != TAILLE_BLOC)
        return -1;
    return 0;
}
