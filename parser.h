/*
 * parser.h — Analyse syntaxique des commandes saisies dans monbash.
 */
#ifndef HEENOKO_PARSER_H
#define HEENOKO_PARSER_H

#include <stddef.h>

#include "common.h"

enum resultat_analyse {
    ANALYSE_ERREUR  = -1,
    ANALYSE_VIDE    = 0,
    ANALYSE_REQUETE = 1,    /* `req` est prête à être envoyée au noyau */
    ANALYSE_AIDE    = 2,
    ANALYSE_QUITTER = 3
};

/* Analyse `ligne` et remplit `req`. En cas d'erreur, `erreur` contient le message. */
int parser_analyser(const char *ligne, struct requete *req, char *erreur, size_t taille_erreur);

void parser_aide(void);

#endif
