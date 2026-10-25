#ifndef HEENOKO_PARSER_H
#define HEENOKO_PARSER_H

#include <stddef.h>

#include "common.h"

enum resultat_analyse {
    ANALYSE_ERREUR  = -1,
    ANALYSE_VIDE    = 0,
    ANALYSE_REQUETE = 1,
    ANALYSE_AIDE    = 2,
    ANALYSE_QUITTER = 3
};

int parser_analyser(const char *ligne, struct requete *req, char *erreur, size_t taille_erreur);

void parser_aide(void);

#endif
