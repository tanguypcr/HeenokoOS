/* Garde d'inclusion (#ifndef / #define / #endif) : empêche une double inclusion de ce fichier. */
#ifndef HEENOKO_DISK_H
#define HEENOKO_DISK_H

/* <stdint.h> : fournit des entiers de taille fixe (uint8_t = 8 bits, uint16_t = 16 bits,
   uint32_t = 32 bits non signés). Contrairement à int, leur taille ne dépend pas du processeur :
   indispensable pour que les structures écrites sur data.bin aient toujours le même format. */
#include <stdint.h>

#define TAILLE_BLOC     512
#define NB_BLOCS        2048
#define TAILLE_DISQUE   (TAILLE_BLOC * NB_BLOCS)

int  disk_ouvrir(const char *chemin);
void disk_fermer(void);

int  lire_bloc(uint32_t num, void *tampon);
int  ecrire_bloc(uint32_t num, const void *tampon);

#endif
