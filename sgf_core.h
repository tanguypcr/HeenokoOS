/*
 * sgf_core.h — Système de Gestion de Fichiers de HeenokoOS.
 *
 * Organisation de data.bin (2048 blocs de 512 octets) :
 *   bloc 0        superbloc + carte d'allocation des blocs
 *   blocs 1 à 4   table des 64 inodes (32 octets chacun)
 *   blocs 5..2047 blocs de données
 */
#ifndef HEENOKO_SGF_CORE_H
#define HEENOKO_SGF_CORE_H

#include <stdint.h>

#include "disk.h"

#define MAGIE_SGF               0x4845454Eu    /* "HEEN" */
#define NB_INODES               64
#define BLOC_INODES             1
#define NB_BLOCS_INODES         4
#define INODES_PAR_BLOC         (TAILLE_BLOC / (int)sizeof(struct inode))
#define PREMIER_BLOC_DONNEES    (BLOC_INODES + NB_BLOCS_INODES)

#define NB_DIRECTS              4
#define NB_INDIRECTS            (TAILLE_BLOC / (int)sizeof(uint32_t))     /* 128 */
#define TAILLE_MAX_FICHIER      ((NB_DIRECTS + NB_INDIRECTS) * TAILLE_BLOC) /* 67 584 */

#define TAILLE_NOM              28      /* 27 caractères + '\0' */
#define INODE_RACINE            0

#define TYPE_LIBRE              0
#define TYPE_FICHIER            1
#define TYPE_REPERTOIRE         2

/* Modes d'ouverture pour _myopen. */
#define MY_LECTURE              0x1
#define MY_ECRITURE             0x2
#define MY_AJOUT                0x4

/* Codes d'erreur (toujours négatifs). */
enum sgf_erreur {
    SGF_OK              =  0,
    ERR_INTROUVABLE     = -1,
    ERR_EXISTE          = -2,
    ERR_PAS_REPERTOIRE  = -3,
    ERR_EST_REPERTOIRE  = -4,
    ERR_NON_VIDE        = -5,
    ERR_DROITS          = -6,
    ERR_PLUS_INODES     = -7,
    ERR_DISQUE_PLEIN    = -8,
    ERR_TROP_GRAND      = -9,
    ERR_NOM_INVALIDE    = -10,
    ERR_NON_OUVERT      = -11,
    ERR_OCCUPE          = -12,
    ERR_ES              = -13,
    ERR_CHEMIN          = -14,
    ERR_MEME_FICHIER    = -15
};

struct superbloc {
    uint32_t magie;
    uint32_t nb_blocs;
    uint32_t nb_inodes;
    uint32_t premier_bloc_donnees;
    uint32_t blocs_libres;
    uint32_t inodes_libres;
    /*
     * Une table d'un octet par bloc (2048 octets) ne tient pas dans le bloc 0 :
     * on utilise donc un bit par bloc (0 = libre, 1 = occupé), soit 256 octets.
     */
    uint8_t  carte_blocs[NB_BLOCS / 8];
};

struct inode {
    uint16_t type;
    uint16_t mode;                      /* permissions rwxrwxrwx */
    uint16_t uid;                       /* propriétaire */
    uint16_t nb_liens;
    uint32_t taille;                    /* en octets */
    uint32_t directs[NB_DIRECTS];
    uint32_t indirect;                  /* bloc d'indirection simple, 0 si absent */
};

struct entree_rep {
    uint32_t inode;
    char     nom[TAILLE_NOM];           /* nom[0] == '\0' : entrée libre */
};

struct sgf_espace {
    uint32_t blocs_total;
    uint32_t blocs_libres;
    uint32_t blocs_systeme;
    uint32_t inodes_total;
    uint32_t inodes_libres;
};

_Static_assert(sizeof(struct superbloc) <= TAILLE_BLOC, "le superbloc doit tenir dans un bloc");
_Static_assert(sizeof(struct inode) == 32, "64 inodes doivent tenir dans 4 blocs");
_Static_assert(sizeof(struct entree_rep) == 32, "16 entrées par bloc de répertoire");

/* Montage : renvoie 1 si le disque a été formaté, 0 sinon, < 0 en cas d'erreur. */
int  sgf_monter(const char *chemin_disque);
void sgf_demonter(void);

/* Primitives. Les noms sont des chemins absolus ou relatifs au répertoire courant. */
int _mycreate(const char *nom, int mode);       /* crée (ou tronque) et ouvre en écriture */
int _myopen(const char *nom, int mode);
int _myclose(int inode);
int _myread(int inode, void *tampon, int n);
int _mywrite(int inode, const void *tampon, int n);
int _mkdir(const char *nom);
int _rmdir(const char *nom);
int _unlink(const char *nom);

/* Services complémentaires utilisés par le noyau. */
typedef void (*sgf_rappel_liste)(const char *nom, int ino, const struct inode *in, void *ctx);

int         sgf_cd(const char *chemin);
const char *sgf_cwd(void);
int         sgf_stat(const char *chemin, struct inode *in);    /* renvoie le numéro d'inode */
int         sgf_lister(const char *chemin, sgf_rappel_liste rappel, void *ctx);
int         sgf_renommer(const char *source, const char *destination);
int         sgf_chmod(const char *chemin, int mode);
void        sgf_espace(struct sgf_espace *e);
const char *sgf_strerror(int code);

#endif
