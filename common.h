/*
 * common.h — Définitions partagées entre monbash, noyau et ecran.
 */
#ifndef HEENOKO_COMMON_H
#define HEENOKO_COMMON_H

#include <stddef.h>

#define FIFO_ECRAN      "/tmp/heenok_screen_fifo"
#define DISQUE_DEFAUT   "data.bin"

#define TAILLE_CHEMIN   256
#define TAILLE_LIGNE    1024
#define TAILLE_DONNEES  1024

/* Opérations transmises du Shell au Noyau. */
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

/* Options de requête (champ `options`). */
#define OPT_LS_LONG     0x1     /* ls -l */
#define OPT_ECHO_FICHIER 0x2    /* echo ... > fichier */
#define OPT_ECHO_AJOUT  0x4     /* echo ... >> fichier */

/* Requête Shell -> Noyau (tube anonyme). */
struct requete {
    int  op;
    int  options;
    int  valeur;                        /* mode pour chmod */
    int  taille;                        /* octets utiles dans donnees */
    char ligne[TAILLE_LIGNE];           /* commande brute, pour l'écho sur l'Écran */
    char chemin[TAILLE_CHEMIN];
    char chemin2[TAILLE_CHEMIN];
    char donnees[TAILLE_DONNEES];
};

/* Accusé de réception Noyau -> Shell, pour mettre à jour l'invite. */
struct reponse {
    int  statut;                        /* 0 = succès, < 0 = code d'erreur SGF */
    char cwd[TAILLE_CHEMIN];
};

#endif
