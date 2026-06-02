/******************************************************
 * node/colors.h                                      *
 * FPAC project.            FPAC NODE                 *
 *                                                    *
 * Parseur de /usr/local/etc/ax25/fpacnode.colors     *
 * Couleurs d'affichage paramétrables                 *
 *                                                    *
 * F6BVP 05-2026                                      *
 *                                                    *
 ******************************************************/

#ifndef COLORS_H
#define COLORS_H

/* Taille max d'une séquence ANSI combinée fg+bg
 * ex: "\033[1;92m\033[101m" = 15 octets + '\0' → 24 par sécurité */
#define COLOR_LEN 24

/* ── Structure principale ───────────────────────────────────────── */

typedef struct {

    /* [banniere] */
    char version[COLOR_LEN];        /* numéro de version au démarrage  */
    char node[COLOR_LEN];           /* nom du node / hostname           */
    char hello[COLOR_LEN];          /* contenu du fichier hello/info    */

    /* [tableaux] */
    char en_tete[COLOR_LEN];        /* en-têtes de colonnes             */
    char indicatif[COLOR_LEN];      /* callsigns AX.25 dans les listes  */
    char adresse[COLOR_LEN];        /* adresses ROSE / NET-ROM          */
    char port[COLOR_LEN];           /* noms de ports                    */
    char date[COLOR_LEN];           /* horodatages / durées             */

    /* [ax25]  — états AX.25 (p->st) */
    char ax25_disconnected[COLOR_LEN]; /* état 0 : déconnecté           */
    char ax25_conn_pending[COLOR_LEN]; /* état 1 : connexion en cours   */
    char ax25_disc_pending[COLOR_LEN]; /* état 2 : déconnexion en cours */
    char ax25_connected[COLOR_LEN];    /* état 3 : connecté             */
    char ax25_recovery[COLOR_LEN];     /* état 4 : récupération         */
    char ax25_unknown[COLOR_LEN];      /* état inconnu (défaut)         */

    /* [rose] */
    char rose_connected[COLOR_LEN]; /* circuit ROSE établi              */
    char rose_idle[COLOR_LEN];      /* pas de circuit ROSE (-----)      */

    /* [netrom] */
    char netrom_connected[COLOR_LEN]; /* voisin NET-ROM joignable       */
    char netrom_idle[COLOR_LEN];      /* voisin non joignable (-----)   */

    /* [qualite] */
    char qualite_bonne[COLOR_LEN];  /* qualité > 50 %                   */
    char qualite_moyenne[COLOR_LEN];/* qualité 1–50 %                   */
    char qualite_nulle[COLOR_LEN];  /* qualité 0 %                      */

    /* [indicateurs] */
    char ind_oui[COLOR_LEN];        /* ex : restart = yes               */
    char ind_non[COLOR_LEN];        /* ex : restart = no                */

} node_colors_t;

/* ── Variable globale ───────────────────────────────────────────── */

extern node_colors_t NodeColors;

/* ── Prototypes ─────────────────────────────────────────────────── */

/* Initialise NodeColors avec les valeurs par défaut (couleurs
 * identiques aux macros codées en dur dans node.h).
 * Appelée automatiquement par read_colors(). */
void colors_set_defaults(void);

/* Lit /usr/local/etc/ax25/fpacnode.colors et remplace les valeurs
 * par défaut par celles du fichier.
 * Retourne 1 si le fichier a été lu, 0 si absent (défauts conservés). */
int read_colors(void);

#endif /* COLORS_H */
