/* #ifndef / #define / #endif : « garde d'inclusion » */
#ifndef HEENOKO_COMMON_H
#define HEENOKO_COMMON_H

/* <stddef.h> : en-tête standard qui définit size_t (le type des tailles en octets). */
#include <stddef.h>

#define FIFO_ECRAN      "/tmp/heenok_screen_fifo"
#define DISQUE_DEFAUT   "data.bin"

#define TAILLE_CHEMIN   256
#define TAILLE_LIGNE    1024
#define TAILLE_DONNEES  1024

enum operation {
    OP_LS,
    OP_CD,
    OP_PWD,
    OP_CAT,
    OP_ECHO,
    OP_CP,
    OP_MV,
    OP_RM,
    OP_MKDIR,
    OP_RMDIR,
    OP_TOUCH,
    OP_CHMOD,
    OP_DF,
    OP_BLING,
    OP_QUITTER
};

#define OPT_LS_LONG     0x1
#define OPT_ECHO_FICHIER 0x2
#define OPT_ECHO_AJOUT  0x4

struct requete {
    int  op;
    int  options;
    int  valeur;
    int  taille;
    char ligne[TAILLE_LIGNE];
    char chemin[TAILLE_CHEMIN];
    char chemin2[TAILLE_CHEMIN];
    char donnees[TAILLE_DONNEES];
};

struct reponse {
    int  statut;
    char cwd[TAILLE_CHEMIN];
};

#endif
