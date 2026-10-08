/*
 * disk.h — Accès bas niveau au disque virtuel data.bin (blocs de 512 octets).
 */
#ifndef HEENOKO_DISK_H
#define HEENOKO_DISK_H

#include <stdint.h>

#define TAILLE_BLOC     512
#define NB_BLOCS        2048
#define TAILLE_DISQUE   (TAILLE_BLOC * NB_BLOCS)    /* 1 Mo */

/* Ouvre le disque. Renvoie 1 s'il vient d'être créé (à formater), 0 s'il existait, -1 en cas d'erreur. */
int  disk_ouvrir(const char *chemin);
void disk_fermer(void);

int  lire_bloc(uint32_t num, void *tampon);
int  ecrire_bloc(uint32_t num, const void *tampon);

#endif
