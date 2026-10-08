/*
 * noyau.c — Processus Noyau de HeenokoOS.
 *
 * Lancé par monbash via fork()/execvp() :
 *     noyau <fd_requetes> <fd_reponses> [disque]
 * Reçoit les requêtes du Shell sur un tube anonyme, les exécute sur le SGF
 * et envoie les résultats à l'Écran par la FIFO /tmp/heenok_screen_fifo.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "sgf_core.h"

#define COULEUR_INVITE  "\033[1;33m"
#define COULEUR_ERREUR  "\033[1;31m"
#define COULEUR_REP     "\033[1;34m"
#define COULEUR_OK      "\033[1;32m"
#define COULEUR_FIN     "\033[0m"

static int fd_ecran = -1;
static int ecran_absent_signale = 0;
static const char *nom_disque = DISQUE_DEFAUT;

/* ------------------------------------------------------------------ */
/* Sortie vers l'Écran                                                 */
/* ------------------------------------------------------------------ */

static int ecrire_complet(int fd, const void *tampon, size_t n)
{
    const char *p = tampon;

    while (n > 0) {
        ssize_t r = write(fd, p, n);

        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

static int lire_complet(int fd, void *tampon, size_t n)
{
    char *p = tampon;

    while (n > 0) {
        ssize_t r = read(fd, p, n);

        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        p += r;
        n -= (size_t)r;
    }
    return 0;
}

/*
 * Ouvre la FIFO sans bloquer : si l'Écran n'est pas lancé, open() échoue
 * (ENXIO) et l'affichage se replie sur la sortie standard du noyau.
 */
static void ecran_connecter(void)
{
    int fd;

    if (fd_ecran >= 0)
        return;
    fd = open(FIFO_ECRAN, O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        if (!ecran_absent_signale) {
            fprintf(stderr, "[noyau] Écran absent (%s) : affichage redirigé vers ce terminal.\n", FIFO_ECRAN);
            ecran_absent_signale = 1;
        }
        return;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
    fd_ecran = fd;
    ecran_absent_signale = 0;
}

static void ecran_ecrire(const char *texte, size_t n)
{
    ecran_connecter();
    if (fd_ecran >= 0) {
        if (ecrire_complet(fd_ecran, texte, n) == 0)
            return;
        /* L'Écran a été fermé : on se reconnectera à la prochaine sortie. */
        close(fd_ecran);
        fd_ecran = -1;
        ecran_connecter();
    }
    ecrire_complet(STDOUT_FILENO, texte, n);
}

static void afficher(const char *format, ...)
{
    char    tampon[4096];
    va_list args;
    int     n;

    va_start(args, format);
    n = vsnprintf(tampon, sizeof tampon, format, args);
    va_end(args);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof tampon)
        n = sizeof tampon - 1;
    ecran_ecrire(tampon, (size_t)n);
}

static void afficher_erreur(const char *commande, const char *chemin, int code)
{
    if (chemin && *chemin)
        afficher(COULEUR_ERREUR "%s: %s : %s" COULEUR_FIN "\n", commande, chemin, sgf_strerror(code));
    else
        afficher(COULEUR_ERREUR "%s: %s" COULEUR_FIN "\n", commande, sgf_strerror(code));
}

/* ------------------------------------------------------------------ */
/* Commandes                                                           */
/* ------------------------------------------------------------------ */

struct ctx_ls {
    int longue;
    int nb;
};

static void permissions(const struct inode *in, char *sortie)
{
    static const char lettres[] = "rwxrwxrwx";

    sortie[0] = (in->type == TYPE_REPERTOIRE) ? 'd' : '-';
    for (int i = 0; i < 9; i++)
        sortie[i + 1] = (in->mode & (0400 >> i)) ? lettres[i] : '-';
    sortie[10] = '\0';
}

static void ls_entree(const char *nom, int ino, const struct inode *in, void *p)
{
    struct ctx_ls *ctx = p;
    const char    *couleur = (in->type == TYPE_REPERTOIRE) ? COULEUR_REP : "";
    const char    *fin = (in->type == TYPE_REPERTOIRE) ? COULEUR_FIN : "";

    if (ctx->longue) {
        char           droits[11];
        char           proprio[16];
        struct passwd *pw = getpwuid(in->uid);

        permissions(in, droits);
        if (pw)
            snprintf(proprio, sizeof proprio, "%s", pw->pw_name);
        else
            snprintf(proprio, sizeof proprio, "%u", in->uid);
        afficher("%s %3d %-10s %6u %s%s%s\n", droits, ino, proprio, in->taille, couleur, nom, fin);
    } else {
        afficher("%s%s%s%s  ", couleur, nom, in->type == TYPE_REPERTOIRE ? "/" : "", fin);
    }
    ctx->nb++;
}

static int cmd_ls(const struct requete *req)
{
    struct ctx_ls ctx = { req->options & OPT_LS_LONG, 0 };
    int           r;

    if (ctx.longue)
        afficher("droits     ino proprio      taille nom\n");
    r = sgf_lister(req->chemin[0] ? req->chemin : ".", ls_entree, &ctx);
    if (r < 0)
        return r;
    if (!ctx.longue && ctx.nb > 0)
        afficher("\n");
    return SGF_OK;
}

static int cmd_cat(const struct requete *req)
{
    char tampon[TAILLE_BLOC];
    char dernier = '\n';
    int  ino, n;

    if ((ino = _myopen(req->chemin, MY_LECTURE)) < 0)
        return ino;
    while ((n = _myread(ino, tampon, sizeof tampon)) > 0) {
        ecran_ecrire(tampon, (size_t)n);
        dernier = tampon[n - 1];
    }
    _myclose(ino);
    if (dernier != '\n')
        afficher("\n");
    return n < 0 ? n : SGF_OK;
}

static int cmd_echo(const struct requete *req)
{
    int ino, n;

    if (!(req->options & OPT_ECHO_FICHIER)) {
        ecran_ecrire(req->donnees, (size_t)req->taille);
        return SGF_OK;
    }

    if (req->options & OPT_ECHO_AJOUT) {
        ino = _myopen(req->chemin, MY_AJOUT);
        if (ino == ERR_INTROUVABLE)
            ino = _mycreate(req->chemin, 0644);
    } else {
        ino = _mycreate(req->chemin, 0644);
    }
    if (ino < 0)
        return ino;
    n = _mywrite(ino, req->donnees, req->taille);
    _myclose(ino);
    return n < 0 ? n : SGF_OK;
}

static int cmd_cp(const struct requete *req)
{
    char         destination[2 * TAILLE_CHEMIN];
    char         tampon[TAILLE_BLOC];
    struct inode in_src, in_dst;
    int          ino_src, ino_dst, n, r = SGF_OK;

    if ((ino_src = sgf_stat(req->chemin, &in_src)) < 0)
        return ino_src;
    if (in_src.type == TYPE_REPERTOIRE)
        return ERR_EST_REPERTOIRE;

    /* cp fichier repertoire/ : copie sous le même nom dans le répertoire. */
    snprintf(destination, sizeof destination, "%s", req->chemin2);
    ino_dst = sgf_stat(destination, &in_dst);
    if (ino_dst >= 0 && in_dst.type == TYPE_REPERTOIRE) {
        const char *base = strrchr(req->chemin, '/');

        base = base ? base + 1 : req->chemin;
        snprintf(destination, sizeof destination, "%s/%s", req->chemin2, base);
        ino_dst = sgf_stat(destination, &in_dst);
    }
    if (strlen(destination) >= TAILLE_CHEMIN)
        return ERR_CHEMIN;
    if (ino_dst == ino_src)
        return ERR_MEME_FICHIER;

    if ((ino_src = _myopen(req->chemin, MY_LECTURE)) < 0)
        return ino_src;
    if ((ino_dst = _mycreate(destination, in_src.mode)) < 0) {
        _myclose(ino_src);
        return ino_dst;
    }
    while ((n = _myread(ino_src, tampon, sizeof tampon)) > 0) {
        int ecrits = _mywrite(ino_dst, tampon, n);

        if (ecrits < 0) {
            r = ecrits;
            break;
        }
        if (ecrits < n) {
            r = ERR_DISQUE_PLEIN;
            break;
        }
    }
    if (n < 0)
        r = n;
    _myclose(ino_src);
    _myclose(ino_dst);
    return r;
}

static int cmd_touch(const struct requete *req)
{
    struct inode in;
    int          ino = sgf_stat(req->chemin, &in);

    if (ino >= 0)
        return SGF_OK;
    if (ino != ERR_INTROUVABLE)
        return ino;
    if ((ino = _mycreate(req->chemin, 0644)) < 0)
        return ino;
    return _myclose(ino);
}

static void afficher_espace(void)
{
    struct sgf_espace e;
    uint32_t          utilises;

    sgf_espace(&e);
    utilises = e.blocs_total - e.blocs_libres;
    afficher("Volume      %s\n", nom_disque);
    afficher("Taille      %u Ko (%u blocs de %d octets)\n",
             e.blocs_total * TAILLE_BLOC / 1024, e.blocs_total, TAILLE_BLOC);
    afficher("Utilisé     %u Ko (%u blocs, dont %u réservés au système)\n",
             utilises * TAILLE_BLOC / 1024, utilises, e.blocs_systeme);
    afficher("Libre       %u Ko (%u blocs)\n", e.blocs_libres * TAILLE_BLOC / 1024, e.blocs_libres);
    afficher("Occupation  %.1f %%\n", 100.0 * utilises / e.blocs_total);
    afficher("Inodes      %u utilisés / %u\n", e.inodes_total - e.inodes_libres, e.inodes_total);
}

static void afficher_bling(void)
{
    struct sgf_espace e;
    char              barre[41];
    uint32_t          donnees, utilises;
    double            taux;
    int               plein;
    const char       *verdict;

    sgf_espace(&e);
    donnees = e.blocs_total - e.blocs_systeme;
    utilises = donnees - e.blocs_libres;
    taux = 100.0 * utilises / donnees;
    plein = (int)(taux * 40 / 100 + 0.5);
    if (utilises > 0 && plein == 0)
        plein = 1;
    for (int i = 0; i < 40; i++)
        barre[i] = (i < plein) ? '$' : '.';
    barre[40] = '\0';

    if (taux < 10)
        verdict = "Le terrain est encore vierge. Faut charbonner.";
    else if (taux < 50)
        verdict = "Le business démarre, ça commence à rapporter.";
    else if (taux < 90)
        verdict = "Rentabilité maximale. 100% pur coton.";
    else
        verdict = "Le coffre déborde. Il est temps d'investir dans un plus gros disque.";

    afficher(COULEUR_INVITE "  $$ BLING — Taux de rentabilité du stockage $$" COULEUR_FIN "\n");
    afficher("  [" COULEUR_OK "%s" COULEUR_FIN "] %.1f %%\n", barre, taux);
    afficher("  %u Ko investis sur %u Ko de terrain, %u inodes sur %u en activité.\n",
             utilises * TAILLE_BLOC / 1024, donnees * TAILLE_BLOC / 1024,
             e.inodes_total - e.inodes_libres, e.inodes_total);
    afficher("  %s\n", verdict);
}

/* Exécute une requête ; renvoie 0 ou un code d'erreur SGF (déjà affiché). */
static int traiter(const struct requete *req)
{
    static const char *noms[] = {
        [OP_LS] = "ls", [OP_CD] = "cd", [OP_PWD] = "pwd", [OP_CAT] = "cat",
        [OP_ECHO] = "echo", [OP_CP] = "cp", [OP_MV] = "mv", [OP_RM] = "rm",
        [OP_MKDIR] = "mkdir", [OP_RMDIR] = "rmdir", [OP_TOUCH] = "touch",
        [OP_CHMOD] = "chmod", [OP_DF] = "df", [OP_BLING] = "bling", [OP_QUITTER] = "exit",
    };
    const char *chemin_erreur = req->chemin;
    int         r = SGF_OK;

    afficher(COULEUR_INVITE "heenoko:%s$" COULEUR_FIN " %s\n", sgf_cwd(), req->ligne);

    switch (req->op) {
    case OP_LS:    r = cmd_ls(req); break;
    case OP_CD:    r = sgf_cd(req->chemin[0] ? req->chemin : "/"); break;
    case OP_PWD:   afficher("%s\n", sgf_cwd()); break;
    case OP_CAT:   r = cmd_cat(req); break;
    case OP_ECHO:  r = cmd_echo(req); break;
    case OP_CP:    r = cmd_cp(req); break;
    case OP_MV:    r = sgf_renommer(req->chemin, req->chemin2); break;
    case OP_RM:    r = _unlink(req->chemin); break;
    case OP_MKDIR: r = _mkdir(req->chemin); break;
    case OP_RMDIR: r = _rmdir(req->chemin); break;
    case OP_TOUCH: r = cmd_touch(req); break;
    case OP_CHMOD: r = sgf_chmod(req->chemin, req->valeur); break;
    case OP_DF:    afficher_espace(); break;
    case OP_BLING: afficher_bling(); break;
    default:
        afficher(COULEUR_ERREUR "noyau: opération inconnue (%d)" COULEUR_FIN "\n", req->op);
        return ERR_CHEMIN;
    }

    if (r < 0) {
        if (req->op == OP_CP || req->op == OP_MV)
            chemin_erreur = NULL;
        afficher_erreur(noms[req->op], chemin_erreur, r);
    }
    return r;
}

/* ------------------------------------------------------------------ */
/* Boucle principale                                                   */
/* ------------------------------------------------------------------ */

static int repondre(int fd, int statut)
{
    struct reponse rep;

    memset(&rep, 0, sizeof rep);
    rep.statut = statut;
    snprintf(rep.cwd, sizeof rep.cwd, "%s", sgf_cwd());
    return ecrire_complet(fd, &rep, sizeof rep);
}

int main(int argc, char *argv[])
{
    struct requete req;
    int            fd_requetes, fd_reponses, r;

    if (argc < 3) {
        fprintf(stderr, "usage : %s <fd_requetes> <fd_reponses> [disque]\n"
                        "Le noyau est lancé automatiquement par monbash.\n", argv[0]);
        return EXIT_FAILURE;
    }
    fd_requetes = atoi(argv[1]);
    fd_reponses = atoi(argv[2]);
    if (argc > 3)
        nom_disque = argv[3];

    /* Ctrl-C dans le terminal du Shell ne doit pas tuer le noyau. */
    signal(SIGINT, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    afficher(COULEUR_OK "[noyau]" COULEUR_FIN " Montage du volume %s... ", nom_disque);
    r = sgf_monter(nom_disque);
    if (r < 0) {
        afficher(COULEUR_ERREUR "échec : %s" COULEUR_FIN "\n", sgf_strerror(r));
        repondre(fd_reponses, r);
        return EXIT_FAILURE;
    }
    afficher("100%% pur coton.\n");
    if (r == 1)
        afficher(COULEUR_OK "[noyau]" COULEUR_FIN " Disque vierge formaté : 1 Mo, %d blocs, %d inodes.\n",
                 NB_BLOCS, NB_INODES);
    afficher(COULEUR_OK "[noyau]" COULEUR_FIN " Noyau opérationnel sur le terrain.\n");
    repondre(fd_reponses, SGF_OK);

    while (lire_complet(fd_requetes, &req, sizeof req) == 0) {
        req.ligne[TAILLE_LIGNE - 1] = '\0';
        req.chemin[TAILLE_CHEMIN - 1] = '\0';
        req.chemin2[TAILLE_CHEMIN - 1] = '\0';
        if (req.taille < 0 || req.taille > TAILLE_DONNEES)
            req.taille = 0;

        if (req.op == OP_QUITTER) {
            afficher(COULEUR_OK "[noyau]" COULEUR_FIN " Démontage de %s. À la prochaine sur le terrain.\n",
                     nom_disque);
            break;
        }
        if (repondre(fd_reponses, traiter(&req)) < 0)
            break;
    }

    sgf_demonter();
    repondre(fd_reponses, SGF_OK);
    if (fd_ecran >= 0)
        close(fd_ecran);
    return EXIT_SUCCESS;
}
