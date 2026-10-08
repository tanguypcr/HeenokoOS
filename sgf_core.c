/*
 * sgf_core.c — Implémentation du SGF : superbloc, inodes, indirection simple,
 * répertoires et primitives _my*.
 */
#define _POSIX_C_SOURCE 200809L

#include "sgf_core.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

static struct superbloc sb;
static char cwd[TAILLE_CHEMIN] = "/";

/* Table des fichiers ouverts, indexée par numéro d'inode. */
static struct {
    int      ouvert;
    int      mode;
    uint32_t position;
} ouverts[NB_INODES];

/* ------------------------------------------------------------------ */
/* Superbloc et carte d'allocation                                     */
/* ------------------------------------------------------------------ */

static int sync_superbloc(void)
{
    uint8_t bloc[TAILLE_BLOC] = {0};

    memcpy(bloc, &sb, sizeof sb);
    return ecrire_bloc(0, bloc) < 0 ? ERR_ES : SGF_OK;
}

static int bloc_occupe(uint32_t n)
{
    return (sb.carte_blocs[n / 8] >> (n % 8)) & 1;
}

static void marquer_bloc(uint32_t n, int occupe)
{
    if (occupe)
        sb.carte_blocs[n / 8] |= (uint8_t)(1u << (n % 8));
    else
        sb.carte_blocs[n / 8] &= (uint8_t)~(1u << (n % 8));
}

/* Renvoie un bloc de données mis à zéro, ou 0 si le disque est plein. */
static uint32_t allouer_bloc(void)
{
    static const uint8_t zeros[TAILLE_BLOC];

    for (uint32_t n = PREMIER_BLOC_DONNEES; n < NB_BLOCS; n++) {
        if (!bloc_occupe(n)) {
            if (ecrire_bloc(n, zeros) < 0)
                return 0;
            marquer_bloc(n, 1);
            sb.blocs_libres--;
            sync_superbloc();
            return n;
        }
    }
    return 0;
}

static void liberer_bloc(uint32_t n)
{
    if (n >= PREMIER_BLOC_DONNEES && n < NB_BLOCS && bloc_occupe(n)) {
        marquer_bloc(n, 0);
        sb.blocs_libres++;
    }
}

/* ------------------------------------------------------------------ */
/* Inodes                                                              */
/* ------------------------------------------------------------------ */

static int lire_inode(int ino, struct inode *in)
{
    uint8_t bloc[TAILLE_BLOC];

    if (ino < 0 || ino >= NB_INODES)
        return ERR_INTROUVABLE;
    if (lire_bloc(BLOC_INODES + ino / INODES_PAR_BLOC, bloc) < 0)
        return ERR_ES;
    memcpy(in, bloc + (ino % INODES_PAR_BLOC) * sizeof *in, sizeof *in);
    return SGF_OK;
}

static int ecrire_inode(int ino, const struct inode *in)
{
    uint8_t  bloc[TAILLE_BLOC];
    uint32_t num = BLOC_INODES + ino / INODES_PAR_BLOC;

    if (lire_bloc(num, bloc) < 0)
        return ERR_ES;
    memcpy(bloc + (ino % INODES_PAR_BLOC) * sizeof *in, in, sizeof *in);
    return ecrire_bloc(num, bloc) < 0 ? ERR_ES : SGF_OK;
}

static int allouer_inode(int type, int mode)
{
    struct inode in;

    for (int ino = 0; ino < NB_INODES; ino++) {
        if (lire_inode(ino, &in) < 0)
            return ERR_ES;
        if (in.type != TYPE_LIBRE)
            continue;
        memset(&in, 0, sizeof in);
        in.type = (uint16_t)type;
        in.mode = (uint16_t)(mode & 0777);
        in.uid = (uint16_t)getuid();
        in.nb_liens = 1;
        if (ecrire_inode(ino, &in) < 0)
            return ERR_ES;
        sb.inodes_libres--;
        sync_superbloc();
        return ino;
    }
    return ERR_PLUS_INODES;
}

/* Libère tous les blocs (directs, indirects et bloc d'indirection) d'un inode. */
static void liberer_blocs(struct inode *in)
{
    for (int i = 0; i < NB_DIRECTS; i++) {
        liberer_bloc(in->directs[i]);
        in->directs[i] = 0;
    }
    if (in->indirect) {
        uint32_t table[NB_INDIRECTS];

        if (lire_bloc(in->indirect, table) == 0)
            for (int i = 0; i < NB_INDIRECTS; i++)
                liberer_bloc(table[i]);
        liberer_bloc(in->indirect);
        in->indirect = 0;
    }
    in->taille = 0;
    sync_superbloc();
}

static void liberer_inode(int ino)
{
    struct inode in;

    if (lire_inode(ino, &in) < 0)
        return;
    liberer_blocs(&in);
    memset(&in, 0, sizeof in);
    ecrire_inode(ino, &in);
    ouverts[ino].ouvert = 0;
    sb.inodes_libres++;
    sync_superbloc();
}

static int a_droit(const struct inode *in, int droit)
{
    int decalage = (in->uid == (uint16_t)getuid()) ? 6 : 0;

    return ((in->mode >> decalage) & droit) == droit;
}

#define DROIT_R 4
#define DROIT_W 2
#define DROIT_X 1

/* ------------------------------------------------------------------ */
/* Accès aux données : blocs directs et indirection simple             */
/* ------------------------------------------------------------------ */

/*
 * Traduit l'indice logique `idx` d'un bloc du fichier en numéro de bloc physique.
 * Si `allouer` est vrai, les blocs manquants (y compris le bloc d'indirection)
 * sont alloués ; l'appelant doit ensuite réécrire l'inode. Renvoie 0 si absent.
 */
static uint32_t bloc_physique(struct inode *in, uint32_t idx, int allouer)
{
    uint32_t table[NB_INDIRECTS];
    uint32_t b;

    if (idx < NB_DIRECTS) {
        if (!in->directs[idx] && allouer)
            in->directs[idx] = allouer_bloc();
        return in->directs[idx];
    }

    idx -= NB_DIRECTS;
    if (idx >= NB_INDIRECTS)
        return 0;
    if (!in->indirect) {
        if (!allouer || !(in->indirect = allouer_bloc()))
            return 0;
    }
    if (lire_bloc(in->indirect, table) < 0)
        return 0;
    if (!table[idx] && allouer) {
        if (!(b = allouer_bloc()))
            return 0;
        table[idx] = b;
        if (ecrire_bloc(in->indirect, table) < 0)
            return 0;
    }
    return table[idx];
}

static int lire_donnees(struct inode *in, uint32_t pos, void *tampon, int n)
{
    uint8_t  bloc[TAILLE_BLOC];
    uint8_t *dst = tampon;
    int      lus = 0;

    if (pos >= in->taille || n <= 0)
        return 0;
    if ((uint32_t)n > in->taille - pos)
        n = (int)(in->taille - pos);

    while (lus < n) {
        uint32_t decalage = pos % TAILLE_BLOC;
        uint32_t morceau = TAILLE_BLOC - decalage;
        uint32_t b = bloc_physique(in, pos / TAILLE_BLOC, 0);

        if (morceau > (uint32_t)(n - lus))
            morceau = (uint32_t)(n - lus);
        if (b == 0)
            memset(bloc, 0, sizeof bloc);
        else if (lire_bloc(b, bloc) < 0)
            return ERR_ES;
        memcpy(dst + lus, bloc + decalage, morceau);
        lus += (int)morceau;
        pos += morceau;
    }
    return lus;
}

/* Écrit dans les blocs de l'inode (alloués à la demande). L'appelant réécrit l'inode. */
static int ecrire_donnees(struct inode *in, uint32_t pos, const void *tampon, int n)
{
    uint8_t        bloc[TAILLE_BLOC];
    const uint8_t *src = tampon;
    int            ecrits = 0;

    if (n <= 0)
        return 0;
    if ((uint64_t)pos + (uint64_t)n > TAILLE_MAX_FICHIER)
        return ERR_TROP_GRAND;

    while (ecrits < n) {
        uint32_t decalage = pos % TAILLE_BLOC;
        uint32_t morceau = TAILLE_BLOC - decalage;
        uint32_t b = bloc_physique(in, pos / TAILLE_BLOC, 1);

        if (b == 0)
            break;
        if (morceau > (uint32_t)(n - ecrits))
            morceau = (uint32_t)(n - ecrits);
        if (lire_bloc(b, bloc) < 0)
            return ERR_ES;
        memcpy(bloc + decalage, src + ecrits, morceau);
        if (ecrire_bloc(b, bloc) < 0)
            return ERR_ES;
        ecrits += (int)morceau;
        pos += morceau;
        if (pos > in->taille)
            in->taille = pos;
    }
    return (ecrits == 0) ? ERR_DISQUE_PLEIN : ecrits;
}

/* ------------------------------------------------------------------ */
/* Répertoires                                                         */
/* ------------------------------------------------------------------ */

/* Cherche `nom` dans le répertoire ; renvoie son inode et sa position dans le répertoire. */
static int dir_chercher(int ino_rep, const char *nom, uint32_t *position)
{
    struct inode      rep;
    struct entree_rep e;
    int               r;

    if ((r = lire_inode(ino_rep, &rep)) < 0)
        return r;
    if (rep.type != TYPE_REPERTOIRE)
        return ERR_PAS_REPERTOIRE;
    for (uint32_t pos = 0; pos + sizeof e <= rep.taille; pos += sizeof e) {
        if (lire_donnees(&rep, pos, &e, sizeof e) != sizeof e)
            return ERR_ES;
        if (e.nom[0] && strncmp(e.nom, nom, TAILLE_NOM) == 0) {
            if (position)
                *position = pos;
            return (int)e.inode;
        }
    }
    return ERR_INTROUVABLE;
}

static int dir_ecrire_entree(int ino_rep, uint32_t pos, const struct entree_rep *e)
{
    struct inode rep;
    int          r;

    if ((r = lire_inode(ino_rep, &rep)) < 0)
        return r;
    r = ecrire_donnees(&rep, pos, e, sizeof *e);
    if (r < 0)
        return r;
    if (r != sizeof *e)
        return ERR_DISQUE_PLEIN;
    return ecrire_inode(ino_rep, &rep);
}

static int dir_ajouter(int ino_rep, const char *nom, int ino)
{
    struct inode      rep;
    struct entree_rep e;
    uint32_t          pos;
    int               r;

    if ((r = lire_inode(ino_rep, &rep)) < 0)
        return r;
    /* Réutilise la première entrée libre, sinon ajoute en fin de répertoire. */
    for (pos = 0; pos + sizeof e <= rep.taille; pos += sizeof e) {
        if (lire_donnees(&rep, pos, &e, sizeof e) != sizeof e)
            return ERR_ES;
        if (!e.nom[0])
            break;
    }
    memset(&e, 0, sizeof e);
    e.inode = (uint32_t)ino;
    strncpy(e.nom, nom, TAILLE_NOM - 1);
    return dir_ecrire_entree(ino_rep, pos, &e);
}

static int dir_retirer(int ino_rep, const char *nom)
{
    struct entree_rep e = {0};
    uint32_t          pos;
    int               r;

    if ((r = dir_chercher(ino_rep, nom, &pos)) < 0)
        return r;
    return dir_ecrire_entree(ino_rep, pos, &e);
}

static int dir_modifier(int ino_rep, const char *nom, int nouvel_ino)
{
    struct entree_rep e = {0};
    uint32_t          pos;
    int               r;

    if ((r = dir_chercher(ino_rep, nom, &pos)) < 0)
        return r;
    e.inode = (uint32_t)nouvel_ino;
    strncpy(e.nom, nom, TAILLE_NOM - 1);
    return dir_ecrire_entree(ino_rep, pos, &e);
}

static int dir_est_vide(int ino_rep)
{
    struct inode      rep;
    struct entree_rep e;

    if (lire_inode(ino_rep, &rep) < 0)
        return 0;
    for (uint32_t pos = 0; pos + sizeof e <= rep.taille; pos += sizeof e) {
        if (lire_donnees(&rep, pos, &e, sizeof e) != sizeof e)
            return 0;
        if (e.nom[0] && strcmp(e.nom, ".") != 0 && strcmp(e.nom, "..") != 0)
            return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Chemins                                                             */
/* ------------------------------------------------------------------ */

/* Transforme un chemin quelconque en chemin absolu canonique (sans ".", ".." ni "//"). */
static int normaliser(const char *chemin, char *sortie)
{
    char  tmp[2 * TAILLE_CHEMIN];
    char *composants[TAILLE_CHEMIN / 2];
    char *mot, *reste;
    int   n = 0;
    size_t len = 1;

    if (chemin[0] == '/')
        snprintf(tmp, sizeof tmp, "%s", chemin);
    else
        snprintf(tmp, sizeof tmp, "%s/%s", cwd, chemin);

    for (mot = strtok_r(tmp, "/", &reste); mot; mot = strtok_r(NULL, "/", &reste)) {
        if (strcmp(mot, ".") == 0)
            continue;
        if (strcmp(mot, "..") == 0) {
            if (n > 0)
                n--;
            continue;
        }
        if (n == (int)(sizeof composants / sizeof *composants))
            return ERR_CHEMIN;
        composants[n++] = mot;
    }

    strcpy(sortie, "/");
    for (int i = 0; i < n; i++) {
        size_t l = strlen(composants[i]);

        if (len + l + 1 >= TAILLE_CHEMIN)
            return ERR_CHEMIN;
        if (i > 0)
            sortie[len++] = '/';
        memcpy(sortie + len, composants[i], l);
        len += l;
        sortie[len] = '\0';
    }
    return SGF_OK;
}

/* Parcourt l'arborescence depuis la racine pour un chemin absolu canonique. */
static int resoudre_absolu(const char *absolu)
{
    char  tmp[TAILLE_CHEMIN];
    char *mot, *reste;
    int   ino = INODE_RACINE;

    snprintf(tmp, sizeof tmp, "%s", absolu);
    for (mot = strtok_r(tmp, "/", &reste); mot; mot = strtok_r(NULL, "/", &reste)) {
        ino = dir_chercher(ino, mot, NULL);
        if (ino < 0)
            return ino;
    }
    return ino;
}

static int resoudre(const char *chemin)
{
    char absolu[TAILLE_CHEMIN];
    int  r;

    if ((r = normaliser(chemin, absolu)) < 0)
        return r;
    return resoudre_absolu(absolu);
}

/* Sépare un chemin absolu canonique en répertoire parent et nom final. */
static void separer(const char *absolu, char *parent, char *nom)
{
    const char *dernier = strrchr(absolu, '/');

    if (dernier == absolu)
        strcpy(parent, "/");
    else
        snprintf(parent, TAILLE_CHEMIN, "%.*s", (int)(dernier - absolu), absolu);
    snprintf(nom, TAILLE_CHEMIN, "%s", dernier + 1);
}

static int nom_valide(const char *nom)
{
    size_t l = strlen(nom);

    return l > 0 && l < TAILLE_NOM && strcmp(nom, ".") != 0 && strcmp(nom, "..") != 0;
}

/* Vrai si `absolu` est `cwd` ou l'un de ses ancêtres. */
static int contient_cwd(const char *absolu)
{
    size_t l = strlen(absolu);

    if (strcmp(absolu, "/") == 0)
        return 1;
    return strncmp(cwd, absolu, l) == 0 && (cwd[l] == '\0' || cwd[l] == '/');
}

/*
 * Prépare la création d'une entrée : calcule le parent et le nom final,
 * vérifie que le parent est un répertoire accessible en écriture.
 */
static int preparer_creation(const char *chemin, char *nom, int *ino_parent)
{
    char         absolu[TAILLE_CHEMIN], parent[TAILLE_CHEMIN];
    struct inode rep;
    int          r;

    if ((r = normaliser(chemin, absolu)) < 0)
        return r;
    separer(absolu, parent, nom);
    if (!nom_valide(nom))
        return ERR_NOM_INVALIDE;
    if ((*ino_parent = resoudre_absolu(parent)) < 0)
        return *ino_parent;
    if ((r = lire_inode(*ino_parent, &rep)) < 0)
        return r;
    if (rep.type != TYPE_REPERTOIRE)
        return ERR_PAS_REPERTOIRE;
    if (!a_droit(&rep, DROIT_W | DROIT_X))
        return ERR_DROITS;
    return SGF_OK;
}

/* ------------------------------------------------------------------ */
/* Montage et formatage                                                */
/* ------------------------------------------------------------------ */

static int initialiser_repertoire(int ino, int ino_parent)
{
    struct entree_rep entrees[2];
    struct inode      rep;
    int               r;

    memset(entrees, 0, sizeof entrees);
    entrees[0].inode = (uint32_t)ino;
    strcpy(entrees[0].nom, ".");
    entrees[1].inode = (uint32_t)ino_parent;
    strcpy(entrees[1].nom, "..");

    if ((r = lire_inode(ino, &rep)) < 0)
        return r;
    if ((r = ecrire_donnees(&rep, 0, entrees, sizeof entrees)) < 0)
        return r;
    return ecrire_inode(ino, &rep);
}

static int formater(void)
{
    static const uint8_t zeros[TAILLE_BLOC];
    int ino;

    memset(&sb, 0, sizeof sb);
    sb.magie = MAGIE_SGF;
    sb.nb_blocs = NB_BLOCS;
    sb.nb_inodes = NB_INODES;
    sb.premier_bloc_donnees = PREMIER_BLOC_DONNEES;
    sb.blocs_libres = NB_BLOCS - PREMIER_BLOC_DONNEES;
    sb.inodes_libres = NB_INODES;
    for (uint32_t n = 0; n < PREMIER_BLOC_DONNEES; n++)
        marquer_bloc(n, 1);
    for (uint32_t n = BLOC_INODES; n < PREMIER_BLOC_DONNEES; n++)
        if (ecrire_bloc(n, zeros) < 0)
            return ERR_ES;
    if (sync_superbloc() < 0)
        return ERR_ES;

    ino = allouer_inode(TYPE_REPERTOIRE, 0755);
    if (ino != INODE_RACINE)
        return ERR_ES;
    return initialiser_repertoire(INODE_RACINE, INODE_RACINE);
}

int sgf_monter(const char *chemin_disque)
{
    uint8_t bloc[TAILLE_BLOC];
    int     nouveau = disk_ouvrir(chemin_disque);
    int     r;

    if (nouveau < 0)
        return ERR_ES;
    memset(ouverts, 0, sizeof ouverts);
    strcpy(cwd, "/");

    if (!nouveau) {
        if (lire_bloc(0, bloc) < 0)
            return ERR_ES;
        memcpy(&sb, bloc, sizeof sb);
        if (sb.magie == MAGIE_SGF && sb.nb_blocs == NB_BLOCS && sb.nb_inodes == NB_INODES)
            return 0;
    }
    if ((r = formater()) < 0)
        return r;
    return 1;
}

void sgf_demonter(void)
{
    sync_superbloc();
    disk_fermer();
}

/* ------------------------------------------------------------------ */
/* Primitives                                                          */
/* ------------------------------------------------------------------ */

int _mycreate(const char *nom, int mode)
{
    char         base[TAILLE_CHEMIN];
    struct inode in;
    int          ino_parent, ino, r;

    if ((r = preparer_creation(nom, base, &ino_parent)) < 0)
        return r;

    ino = dir_chercher(ino_parent, base, NULL);
    if (ino >= 0) {
        /* Le fichier existe déjà : il est tronqué, comme avec creat(2). */
        if ((r = lire_inode(ino, &in)) < 0)
            return r;
        if (in.type == TYPE_REPERTOIRE)
            return ERR_EST_REPERTOIRE;
        if (!a_droit(&in, DROIT_W))
            return ERR_DROITS;
        liberer_blocs(&in);
        if ((r = ecrire_inode(ino, &in)) < 0)
            return r;
    } else {
        if ((ino = allouer_inode(TYPE_FICHIER, mode)) < 0)
            return ino;
        if ((r = dir_ajouter(ino_parent, base, ino)) < 0) {
            liberer_inode(ino);
            return r;
        }
    }

    ouverts[ino].ouvert = 1;
    ouverts[ino].mode = MY_ECRITURE;
    ouverts[ino].position = 0;
    return ino;
}

int _myopen(const char *nom, int mode)
{
    struct inode in;
    int          ino, r;

    if ((ino = resoudre(nom)) < 0)
        return ino;
    if ((r = lire_inode(ino, &in)) < 0)
        return r;
    if (in.type == TYPE_REPERTOIRE)
        return ERR_EST_REPERTOIRE;
    if ((mode & MY_LECTURE) && !a_droit(&in, DROIT_R))
        return ERR_DROITS;
    if ((mode & (MY_ECRITURE | MY_AJOUT)) && !a_droit(&in, DROIT_W))
        return ERR_DROITS;

    ouverts[ino].ouvert = 1;
    ouverts[ino].mode = (mode & MY_AJOUT) ? (mode | MY_ECRITURE) : mode;
    ouverts[ino].position = (mode & MY_AJOUT) ? in.taille : 0;
    return ino;
}

int _myclose(int ino)
{
    if (ino < 0 || ino >= NB_INODES || !ouverts[ino].ouvert)
        return ERR_NON_OUVERT;
    ouverts[ino].ouvert = 0;
    return sync_superbloc();
}

int _myread(int ino, void *tampon, int n)
{
    struct inode in;
    int          lus;

    if (ino < 0 || ino >= NB_INODES || !ouverts[ino].ouvert || !(ouverts[ino].mode & MY_LECTURE))
        return ERR_NON_OUVERT;
    if ((lus = lire_inode(ino, &in)) < 0)
        return lus;
    lus = lire_donnees(&in, ouverts[ino].position, tampon, n);
    if (lus > 0)
        ouverts[ino].position += (uint32_t)lus;
    return lus;
}

int _mywrite(int ino, const void *tampon, int n)
{
    struct inode in;
    int          ecrits, r;

    if (ino < 0 || ino >= NB_INODES || !ouverts[ino].ouvert || !(ouverts[ino].mode & MY_ECRITURE))
        return ERR_NON_OUVERT;
    if ((r = lire_inode(ino, &in)) < 0)
        return r;
    ecrits = ecrire_donnees(&in, ouverts[ino].position, tampon, n);
    /* L'inode est réécrit même en cas d'erreur : des blocs ont pu être alloués. */
    if ((r = ecrire_inode(ino, &in)) < 0)
        return r;
    if (ecrits > 0)
        ouverts[ino].position += (uint32_t)ecrits;
    return ecrits;
}

int _mkdir(const char *nom)
{
    char base[TAILLE_CHEMIN];
    int  ino_parent, ino, r;

    if ((r = preparer_creation(nom, base, &ino_parent)) < 0)
        return r;
    if (dir_chercher(ino_parent, base, NULL) >= 0)
        return ERR_EXISTE;
    if ((ino = allouer_inode(TYPE_REPERTOIRE, 0755)) < 0)
        return ino;
    if ((r = initialiser_repertoire(ino, ino_parent)) < 0 ||
        (r = dir_ajouter(ino_parent, base, ino)) < 0) {
        liberer_inode(ino);
        return r;
    }
    return SGF_OK;
}

int _rmdir(const char *nom)
{
    char         absolu[TAILLE_CHEMIN], parent[TAILLE_CHEMIN], base[TAILLE_CHEMIN];
    struct inode in, rep;
    int          ino, ino_parent, r;

    if ((r = normaliser(nom, absolu)) < 0)
        return r;
    if ((ino = resoudre_absolu(absolu)) < 0)
        return ino;
    if ((r = lire_inode(ino, &in)) < 0)
        return r;
    if (in.type != TYPE_REPERTOIRE)
        return ERR_PAS_REPERTOIRE;
    if (contient_cwd(absolu))
        return ERR_OCCUPE;
    if (!dir_est_vide(ino))
        return ERR_NON_VIDE;

    separer(absolu, parent, base);
    ino_parent = resoudre_absolu(parent);
    if ((r = lire_inode(ino_parent, &rep)) < 0)
        return r;
    if (!a_droit(&rep, DROIT_W | DROIT_X))
        return ERR_DROITS;
    if ((r = dir_retirer(ino_parent, base)) < 0)
        return r;
    liberer_inode(ino);
    return SGF_OK;
}

int _unlink(const char *nom)
{
    char         absolu[TAILLE_CHEMIN], parent[TAILLE_CHEMIN], base[TAILLE_CHEMIN];
    struct inode in, rep;
    int          ino, ino_parent, r;

    if ((r = normaliser(nom, absolu)) < 0)
        return r;
    if ((ino = resoudre_absolu(absolu)) < 0)
        return ino;
    if ((r = lire_inode(ino, &in)) < 0)
        return r;
    if (in.type == TYPE_REPERTOIRE)
        return ERR_EST_REPERTOIRE;
    if (ouverts[ino].ouvert)
        return ERR_OCCUPE;

    separer(absolu, parent, base);
    ino_parent = resoudre_absolu(parent);
    if ((r = lire_inode(ino_parent, &rep)) < 0)
        return r;
    if (!a_droit(&rep, DROIT_W | DROIT_X))
        return ERR_DROITS;
    if ((r = dir_retirer(ino_parent, base)) < 0)
        return r;
    liberer_inode(ino);
    return SGF_OK;
}

/* ------------------------------------------------------------------ */
/* Services complémentaires                                            */
/* ------------------------------------------------------------------ */

int sgf_cd(const char *chemin)
{
    char         absolu[TAILLE_CHEMIN];
    struct inode in;
    int          ino, r;

    if ((r = normaliser(chemin, absolu)) < 0)
        return r;
    if ((ino = resoudre_absolu(absolu)) < 0)
        return ino;
    if ((r = lire_inode(ino, &in)) < 0)
        return r;
    if (in.type != TYPE_REPERTOIRE)
        return ERR_PAS_REPERTOIRE;
    if (!a_droit(&in, DROIT_X))
        return ERR_DROITS;
    strcpy(cwd, absolu);
    return SGF_OK;
}

const char *sgf_cwd(void)
{
    return cwd;
}

int sgf_stat(const char *chemin, struct inode *in)
{
    int ino, r;

    if ((ino = resoudre(chemin)) < 0)
        return ino;
    if ((r = lire_inode(ino, in)) < 0)
        return r;
    return ino;
}

int sgf_lister(const char *chemin, sgf_rappel_liste rappel, void *ctx)
{
    char              absolu[TAILLE_CHEMIN], parent[TAILLE_CHEMIN], base[TAILLE_CHEMIN];
    struct inode      rep, in;
    struct entree_rep e;
    int               ino, r;

    if ((r = normaliser(chemin, absolu)) < 0)
        return r;
    if ((ino = resoudre_absolu(absolu)) < 0)
        return ino;
    if ((r = lire_inode(ino, &rep)) < 0)
        return r;

    if (rep.type != TYPE_REPERTOIRE) {
        separer(absolu, parent, base);
        rappel(base, ino, &rep, ctx);
        return SGF_OK;
    }
    if (!a_droit(&rep, DROIT_R))
        return ERR_DROITS;

    for (uint32_t pos = 0; pos + sizeof e <= rep.taille; pos += sizeof e) {
        if (lire_donnees(&rep, pos, &e, sizeof e) != sizeof e)
            return ERR_ES;
        if (!e.nom[0] || strcmp(e.nom, ".") == 0 || strcmp(e.nom, "..") == 0)
            continue;
        if (lire_inode((int)e.inode, &in) < 0)
            continue;
        rappel(e.nom, (int)e.inode, &in, ctx);
    }
    return SGF_OK;
}

int sgf_renommer(const char *source, const char *destination)
{
    char         src[TAILLE_CHEMIN], dst[TAILLE_CHEMIN];
    char         parent_src[TAILLE_CHEMIN], base_src[TAILLE_CHEMIN];
    char         parent_dst[TAILLE_CHEMIN], base_dst[TAILLE_CHEMIN];
    struct inode in_src, in_dst, rep;
    int          ino_src, ino_dst, ino_psrc, ino_pdst, r;
    size_t       l;

    if ((r = normaliser(source, src)) < 0 || (r = normaliser(destination, dst)) < 0)
        return r;
    if ((ino_src = resoudre_absolu(src)) < 0)
        return ino_src;
    if ((r = lire_inode(ino_src, &in_src)) < 0)
        return r;
    if (in_src.type == TYPE_REPERTOIRE && contient_cwd(src))
        return ERR_OCCUPE;
    separer(src, parent_src, base_src);

    /* Déplacement dans un répertoire existant : on garde le nom d'origine. */
    ino_dst = resoudre_absolu(dst);
    if (ino_dst >= 0 && lire_inode(ino_dst, &in_dst) == 0 && in_dst.type == TYPE_REPERTOIRE) {
        l = strlen(dst);
        if (l + 1 + strlen(base_src) >= TAILLE_CHEMIN)
            return ERR_CHEMIN;
        snprintf(dst + l, TAILLE_CHEMIN - l, "%s%s", strcmp(dst, "/") ? "/" : "", base_src);
        ino_dst = resoudre_absolu(dst);
    }
    if (ino_dst == ino_src)
        return SGF_OK;

    /* Interdit de déplacer un répertoire dans sa propre descendance. */
    l = strlen(src);
    if (in_src.type == TYPE_REPERTOIRE && strncmp(dst, src, l) == 0 && dst[l] == '/')
        return ERR_CHEMIN;

    separer(dst, parent_dst, base_dst);
    if (!nom_valide(base_dst))
        return ERR_NOM_INVALIDE;
    if ((ino_pdst = resoudre_absolu(parent_dst)) < 0)
        return ino_pdst;
    if ((r = lire_inode(ino_pdst, &rep)) < 0)
        return r;
    if (rep.type != TYPE_REPERTOIRE)
        return ERR_PAS_REPERTOIRE;
    if (!a_droit(&rep, DROIT_W | DROIT_X))
        return ERR_DROITS;
    ino_psrc = resoudre_absolu(parent_src);
    if ((r = lire_inode(ino_psrc, &rep)) < 0)
        return r;
    if (!a_droit(&rep, DROIT_W | DROIT_X))
        return ERR_DROITS;

    /* Une destination existante n'est écrasée que si ce sont deux fichiers. */
    if (ino_dst >= 0) {
        if ((r = lire_inode(ino_dst, &in_dst)) < 0)
            return r;
        if (in_dst.type == TYPE_REPERTOIRE || in_src.type == TYPE_REPERTOIRE)
            return ERR_EXISTE;
        if (ouverts[ino_dst].ouvert)
            return ERR_OCCUPE;
        if ((r = dir_retirer(ino_pdst, base_dst)) < 0)
            return r;
        liberer_inode(ino_dst);
    }

    if ((r = dir_ajouter(ino_pdst, base_dst, ino_src)) < 0)
        return r;
    if ((r = dir_retirer(ino_psrc, base_src)) < 0)
        return r;
    if (in_src.type == TYPE_REPERTOIRE && ino_psrc != ino_pdst)
        return dir_modifier(ino_src, "..", ino_pdst);
    return SGF_OK;
}

int sgf_chmod(const char *chemin, int mode)
{
    struct inode in;
    int          ino;

    if ((ino = sgf_stat(chemin, &in)) < 0)
        return ino;
    if (in.uid != (uint16_t)getuid())
        return ERR_DROITS;
    in.mode = (uint16_t)(mode & 0777);
    return ecrire_inode(ino, &in);
}

void sgf_espace(struct sgf_espace *e)
{
    e->blocs_total = sb.nb_blocs;
    e->blocs_libres = sb.blocs_libres;
    e->blocs_systeme = sb.premier_bloc_donnees;
    e->inodes_total = sb.nb_inodes;
    e->inodes_libres = sb.inodes_libres;
}

const char *sgf_strerror(int code)
{
    switch (code) {
    case SGF_OK:             return "succès";
    case ERR_INTROUVABLE:    return "fichier ou répertoire introuvable";
    case ERR_EXISTE:         return "le fichier existe déjà";
    case ERR_PAS_REPERTOIRE: return "n'est pas un répertoire";
    case ERR_EST_REPERTOIRE: return "est un répertoire";
    case ERR_NON_VIDE:       return "répertoire non vide";
    case ERR_DROITS:         return "permission refusée";
    case ERR_PLUS_INODES:    return "plus d'inode libre (64 max)";
    case ERR_DISQUE_PLEIN:   return "disque plein";
    case ERR_TROP_GRAND:     return "fichier trop grand (67 584 octets max)";
    case ERR_NOM_INVALIDE:   return "nom invalide (1 à 27 caractères)";
    case ERR_NON_OUVERT:     return "fichier non ouvert";
    case ERR_OCCUPE:         return "ressource occupée";
    case ERR_ES:             return "erreur d'entrée/sortie sur le disque";
    case ERR_CHEMIN:         return "chemin invalide";
    case ERR_MEME_FICHIER:   return "la source et la destination sont identiques";
    default:                 return "erreur inconnue";
    }
}
