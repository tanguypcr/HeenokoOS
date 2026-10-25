#include "parser.h"

/* <ctype.h> : fonctions de classification des caractères, ici isspace(). */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MOTS 64

struct commande {
    const char *nom;
    int         op;
    int         min_args;
    int         max_args;
    const char *usage;
};

/* static devant une variable globale ou une fonction : elle n'est visible que dans ce
   fichier. Le reste du programme n'utilise que parser_analyser() et parser_aide() */
static const struct commande commandes[] = {
    { "ls",    OP_LS,    0, 2, "ls [-l] [chemin]" },
    { "cd",    OP_CD,    0, 1, "cd [chemin]" },
    { "pwd",   OP_PWD,   0, 0, "pwd" },
    { "cat",   OP_CAT,   1, 1, "cat <fichier>" },
    { "echo",  OP_ECHO,  0, MAX_MOTS, "echo \"texte\" [> | >> fichier]" },
    { "cp",    OP_CP,    2, 2, "cp <source> <destination>" },
    { "mv",    OP_MV,    2, 2, "mv <source> <destination>" },
    { "rm",    OP_RM,    1, 1, "rm <fichier>" },
    { "mkdir", OP_MKDIR, 1, 1, "mkdir <répertoire>" },
    { "rmdir", OP_RMDIR, 1, 1, "rmdir <répertoire>" },
    { "touch", OP_TOUCH, 1, 1, "touch <fichier>" },
    { "chmod", OP_CHMOD, 2, 2, "chmod <mode octal> <chemin>" },
    { "df",    OP_DF,    0, 0, "df" },
    { "bling", OP_BLING, 0, 0, "bling" },
};

static int erreur(char *msg, size_t taille, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vsnprintf(msg, taille, format, args);
    va_end(args);
    return ANALYSE_ERREUR;
}

static int decouper(const char *s, char *tampon, char **mots, int max)
{
    char *e = tampon;
    int   n = 0;

    for (;;) {
        /* isspace() : vrai pour un caractère d'espacement (espace, tabulation, retour à la
           ligne...). La conversion en unsigned char est exigée par la norme : un char négatif
           (lettre accentuée) passé directement donnerait un comportement indéfini */
        while (isspace((unsigned char)*s))
            s++;
        if (!*s)
            break;
        if (n == max)
            return -1;
        mots[n++] = e;

        if (*s == '>') {
            *e++ = *s++;
            if (*s == '>')
                *e++ = *s++;
            *e++ = '\0';
            continue;
        }
        while (*s && !isspace((unsigned char)*s) && *s != '>') {
            if (*s == '"' || *s == '\'') {
                char guillemet = *s++;

                while (*s && *s != guillemet)
                    *e++ = *s++;
                if (!*s)
                    return -2;
                s++;
            } else {
                *e++ = *s++;
            }
        }
        *e++ = '\0';
    }
    return n;
}

static int copier_chemin(char *dst, const char *src, char *msg, size_t taille)
{
    /* strlen() : renvoie la longueur d'une chaîne, sans compter le '\0' final. On vérifie
       que le chemin tient dans le tableau avant le strcpy(), pour éviter un débordement */
    if (strlen(src) >= TAILLE_CHEMIN)
        /* %.40s : n'affiche que les 40 premiers caractères de la chaîne. */
        return erreur(msg, taille, "chemin trop long : %.40s...", src);
    strcpy(dst, src);
    return 0;
}

static int analyser_echo(char **args, int nb, struct requete *req, char *msg, size_t taille)
{
    size_t len = 0;
    int    i;

    /* strcmp() : compare deux chaînes et renvoie 0 si elles sont identiques */
    for (i = 0; i < nb && strcmp(args[i], ">") != 0 && strcmp(args[i], ">>") != 0; i++) {
        size_t l = strlen(args[i]);

        if (len + l + 2 > TAILLE_DONNEES)
            return erreur(msg, taille, "echo : texte trop long (%d octets max)", TAILLE_DONNEES - 1);
        if (i > 0)
            req->donnees[len++] = ' ';
        /* memcpy() : copie un nombre précis d'octets d'une zone mémoire vers une autre. Ici on
           colle chaque mot à la suite du précédent, sans '\0' entre eux */
        memcpy(req->donnees + len, args[i], l);
        len += l;
    }
    req->donnees[len++] = '\n';
    req->taille = (int)len;

    if (i < nb) {
        /* |= : forme abrégée de req->options = req->options | OPT_... ; positionne un bit
           d'option sans effacer les autres */
        req->options |= OPT_ECHO_FICHIER;
        if (strcmp(args[i], ">>") == 0)
            req->options |= OPT_ECHO_AJOUT;
        if (i + 2 != nb)
            return erreur(msg, taille, "echo : un seul fichier attendu après %s", args[i]);
        return copier_chemin(req->chemin, args[i + 1], msg, taille);
    }
    return 0;
}

int parser_analyser(const char *ligne, struct requete *req, char *msg, size_t taille)
{
    char                   tampon[2 * TAILLE_LIGNE];
    char                  *mots[MAX_MOTS];
    char                 **args;
    const struct commande *cmd = NULL;
    int                    nb, i;

    if (strlen(ligne) >= TAILLE_LIGNE)
        return erreur(msg, taille, "ligne trop longue (%d caractères max)", TAILLE_LIGNE - 1);

    nb = decouper(ligne, tampon, mots, MAX_MOTS);
    if (nb == -1)
        return erreur(msg, taille, "trop d'arguments (%d max)", MAX_MOTS);
    if (nb == -2)
        return erreur(msg, taille, "guillemet non fermé");
    if (nb == 0)
        return ANALYSE_VIDE;

    if (strcmp(mots[0], "exit") == 0 || strcmp(mots[0], "quit") == 0)
        return ANALYSE_QUITTER;
    if (strcmp(mots[0], "help") == 0 || strcmp(mots[0], "aide") == 0)
        return ANALYSE_AIDE;

    for (i = 0; i < (int)(sizeof commandes / sizeof *commandes); i++)
        if (strcmp(mots[0], commandes[i].nom) == 0)
            cmd = &commandes[i];
    if (!cmd)
        return erreur(msg, taille, "%s : commande inconnue (tapez help)", mots[0]);

    args = mots + 1;
    nb--;
    if (nb < cmd->min_args || nb > cmd->max_args)
        return erreur(msg, taille, "usage : %s", cmd->usage);

    memset(req, 0, sizeof *req);
    req->op = cmd->op;
    snprintf(req->ligne, sizeof req->ligne, "%s", ligne);
    /* strcspn() : renvoie la position du premier caractère de la chaîne qui appartient à
       l'ensemble donné. On y place un '\0' pour supprimer le retour à la ligne laissé par
       fgets(). */
    req->ligne[strcspn(req->ligne, "\r\n")] = '\0';

    switch (cmd->op) {
    case OP_LS:
        for (i = 0; i < nb; i++) {
            if (strcmp(args[i], "-l") == 0)
                req->options |= OPT_LS_LONG;
            else if (args[i][0] == '-')
                return erreur(msg, taille, "ls : option inconnue %s", args[i]);
            else if (req->chemin[0])
                return erreur(msg, taille, "usage : %s", cmd->usage);
            else if (copier_chemin(req->chemin, args[i], msg, taille) < 0)
                return ANALYSE_ERREUR;
        }
        break;

    case OP_ECHO:
        if (analyser_echo(args, nb, req, msg, taille) < 0)
            return ANALYSE_ERREUR;
        break;

    case OP_CHMOD: {
        char *fin;
        /* strtol() : convertit une chaîne en entier long dans la base choisie (ici 8, pour
           lire un mode octal comme 755) */
        long  mode = strtol(args[0], &fin, 8);

        if (*fin || fin == args[0] || mode < 0 || mode > 0777)
            return erreur(msg, taille, "chmod : mode octal invalide « %s »", args[0]);
        req->valeur = (int)mode;
        if (copier_chemin(req->chemin, args[1], msg, taille) < 0)
            return ANALYSE_ERREUR;
        break;
    }

    default:
        if (nb >= 1 && copier_chemin(req->chemin, args[0], msg, taille) < 0)
            return ANALYSE_ERREUR;
        if (nb >= 2 && copier_chemin(req->chemin2, args[1], msg, taille) < 0)
            return ANALYSE_ERREUR;
        break;
    }
    return ANALYSE_REQUETE;
}

void parser_aide(void)
{
    printf("Commandes HeenokoOS (les résultats s'affichent dans la fenêtre Écran) :\n");
    for (size_t i = 0; i < sizeof commandes / sizeof *commandes; i++)
        printf("  %s\n", commandes[i].usage);
    printf("  help\n  exit\n");
}
