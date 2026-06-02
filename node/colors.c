/******************************************************
 * node/colors.c                                      *
 * FPAC project.            FPAC NODE                 *
 *                                                    *
 * Parseur de /usr/local/etc/ax25/fpacnode.colors     *
 * Couleurs d'affichage paramétrables                 *
 *                                                    *
 * F6BVP 05-2026                                      *
 *                                                    *
 ******************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "node.h"
#include "colors.h"

#define FPACCOLORS      "/usr/local/etc/ax25/fpacnode.colors"
#define LINE_BUF        256
#define KEY_BUF         64
#define VAL_BUF         128
#define SECT_BUF        32

/* ── Variable globale ───────────────────────────────────────────── */

node_colors_t NodeColors;

/* ── Tables de correspondance nom → séquence ANSI ──────────────── */

typedef struct { const char *name; const char *seq; } ansi_entry_t;

static const ansi_entry_t fg_table[] = {
    { "defaut",         "\033[0;39m" },
    { "noir",           "\033[0;30m" },
    { "rouge_fonce",    "\033[0;31m" },
    { "vert_fonce",     "\033[0;32m" },
    { "jaune_fonce",    "\033[0;33m" },
    { "bleu_fonce",     "\033[0;34m" },
    { "magenta_fonce",  "\033[0;35m" },
    { "cyan_fonce",     "\033[0;36m" },
    { "gris",           "\033[0;37m" },
    { "gris_fonce",     "\033[1;90m" },
    { "rouge",          "\033[1;91m" },
    { "vert",           "\033[1;92m" },
    { "jaune",          "\033[1;93m" },
    { "bleu",           "\033[1;94m" },
    { "magenta",        "\033[1;95m" },
    { "cyan",           "\033[1;96m" },
    { "blanc",          "\033[1;97m" },
    { NULL,             NULL         }
};

static const ansi_entry_t bg_table[] = {
    { "fond_defaut",        "\033[49m"  },
    { "fond_noir",          "\033[40m"  },
    { "fond_rouge_fonce",   "\033[41m"  },
    { "fond_vert_fonce",    "\033[42m"  },
    { "fond_jaune_fonce",   "\033[43m"  },
    { "fond_bleu_fonce",    "\033[44m"  },
    { "fond_magenta_fonce", "\033[45m"  },
    { "fond_cyan_fonce",    "\033[46m"  },
    { "fond_gris",          "\033[47m"  },
    { "fond_gris_fonce",    "\033[100m" },
    { "fond_rouge",         "\033[101m" },
    { "fond_vert",          "\033[102m" },
    { "fond_jaune",         "\033[103m" },
    { "fond_bleu",          "\033[104m" },
    { "fond_magenta",       "\033[105m" },
    { "fond_cyan",          "\033[106m" },
    { "fond_blanc",         "\033[107m" },
    { NULL,                 NULL        }
};

/* ── Fonctions internes ─────────────────────────────────────────── */

/* Supprime les espaces en début et fin de chaîne (in-place).
 * Retourne un pointeur vers le premier caractère non-espace. */
static char *strtrim(char *s)
{
    char *end;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return s;
}

/* Cherche 'name' dans une table ANSI, retourne la séquence ou "". */
static const char *lookup(const ansi_entry_t *table, const char *name)
{
    int i;
    for (i = 0; table[i].name != NULL; i++)
        if (strcmp(table[i].name, name) == 0)
            return table[i].seq;
    return "";
}

/* Convertit une valeur textuelle ("vert + fond_rouge") en séquence ANSI
 * et la stocke dans 'out' (taille 'outsize').
 * Si la couleur est inconnue, 'out' est laissé inchangé (défaut conservé). */
static void parse_color(const char *value, char *out, size_t outsize)
{
    char        buf[VAL_BUF];
    char       *fg_part, *bg_part, *plus;
    const char *fg_seq, *bg_seq;

    strncpy(buf, value, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    plus = strchr(buf, '+');
    if (plus != NULL) {
        *plus  = '\0';
        fg_part = strtrim(buf);
        bg_part = strtrim(plus + 1);
        fg_seq  = lookup(fg_table, fg_part);
        bg_seq  = lookup(bg_table, bg_part);
    } else {
        fg_part = strtrim(buf);
        fg_seq  = lookup(fg_table, fg_part);
        bg_seq  = "";
    }

    /* N'écrit que si au moins le fg est reconnu */
    if (*fg_seq != '\0')
        snprintf(out, outsize, "%s%s", fg_seq, bg_seq);
}

/* ── Valeurs par défaut ─────────────────────────────────────────── */
/* Reproduisent exactement les macros codées en dur dans node.h      */

void colors_set_defaults(void)
{
    /* [banniere] */
    snprintf(NodeColors.version,           COLOR_LEN, "%s", F_Red);
    snprintf(NodeColors.node,              COLOR_LEN, "%s", F_Yellow);
    snprintf(NodeColors.hello,             COLOR_LEN, "%s", F_Blue);

    /* [tableaux] */
    snprintf(NodeColors.en_tete,           COLOR_LEN, "%s", F_Yellow);
    snprintf(NodeColors.indicatif,         COLOR_LEN, "%s", F_Green);
    snprintf(NodeColors.adresse,           COLOR_LEN, "%s", F_Green);
    snprintf(NodeColors.port,              COLOR_LEN, "%s", F_Default);
    snprintf(NodeColors.date,              COLOR_LEN, "%s", F_Cyan);

    /* [ax25] */
    snprintf(NodeColors.ax25_disconnected, COLOR_LEN, "%s%s", F_Green,  B_Red);
    snprintf(NodeColors.ax25_conn_pending, COLOR_LEN, "%s%s", F_Yellow, B_Blue);
    snprintf(NodeColors.ax25_disc_pending, COLOR_LEN, "%s%s", F_Yellow, B_Magenta);
    snprintf(NodeColors.ax25_connected,    COLOR_LEN, "%s%s", F_Black,  B_Green);
    snprintf(NodeColors.ax25_recovery,     COLOR_LEN, "%s%s", F_Blue,   B_Yellow);
    snprintf(NodeColors.ax25_unknown,      COLOR_LEN, "%s%s", F_White,  B_Gray);

    /* [rose] */
    snprintf(NodeColors.rose_connected,    COLOR_LEN, "%s%s", F_Black,  B_Green);
    snprintf(NodeColors.rose_idle,         COLOR_LEN, "%s%s", F_Green,  B_Red);

    /* [netrom] */
    snprintf(NodeColors.netrom_connected,  COLOR_LEN, "%s%s", F_Black,  B_Green);
    snprintf(NodeColors.netrom_idle,       COLOR_LEN, "%s%s", F_Yellow, B_Magenta);

    /* [qualite] */
    snprintf(NodeColors.qualite_bonne,     COLOR_LEN, "%s", F_Green);
    snprintf(NodeColors.qualite_moyenne,   COLOR_LEN, "%s", F_Yellow);
    snprintf(NodeColors.qualite_nulle,     COLOR_LEN, "%s", F_Red);

    /* [indicateurs] */
    snprintf(NodeColors.ind_oui,           COLOR_LEN, "%s%s", F_Black,  B_Green);
    snprintf(NodeColors.ind_non,           COLOR_LEN, "%s%s", F_Black,  B_Red);
}

/* ── Parseur principal ──────────────────────────────────────────── */

int read_colors(void)
{
    FILE *fp;
    char  line[LINE_BUF];
    char  key[KEY_BUF];
    char  value[VAL_BUF];
    char  section[SECT_BUF] = "";
    char *p, *eq, *hash, *end;

    /* Toujours initialiser avec les valeurs par défaut */
    colors_set_defaults();

    fp = fopen(FPACCOLORS, "r");
    if (fp == NULL)
        return 0;   /* fichier absent : on garde les défauts */

    while (fgets(line, sizeof(line), fp) != NULL) {

        /* Supprimer le '\n' / '\r' final */
        end = line + strlen(line) - 1;
        while (end >= line && (*end == '\n' || *end == '\r'))
            *end-- = '\0';

        /* Ignorer les espaces en début de ligne */
        p = line;
        while (isspace((unsigned char)*p)) p++;

        /* Ligne vide ou commentaire */
        if (*p == '\0' || *p == '#') continue;

        /* En-tête de section  [section] */
        if (*p == '[') {
            end = strchr(p, ']');
            if (end != NULL) {
                *end = '\0';
                strncpy(section, p + 1, sizeof(section) - 1);
                section[sizeof(section) - 1] = '\0';
            }
            continue;
        }

        /* Ligne clé = valeur  [# commentaire ignoré] */
        eq = strchr(p, '=');
        if (eq == NULL) continue;

        /* Clé */
        *eq = '\0';
        strncpy(key, strtrim(p), sizeof(key) - 1);
        key[sizeof(key) - 1] = '\0';

        /* Valeur : tout ce qui est après '=' jusqu'au premier '#' */
        p = eq + 1;
        hash = strchr(p, '#');
        if (hash != NULL) *hash = '\0';
        strncpy(value, strtrim(p), sizeof(value) - 1);
        value[sizeof(value) - 1] = '\0';

        if (*key == '\0' || *value == '\0') continue;

        /* ── Dispatch section / clé → champ de NodeColors ── */

#define SET(sect, k, field) \
        else if (strcmp(section, (sect)) == 0 && strcmp(key, (k)) == 0) \
            parse_color(value, NodeColors.field, COLOR_LEN)

        if (0);
        /* [banniere] */
        SET("banniere",    "version",      version);
        SET("banniere",    "node",         node);
        SET("banniere",    "hello",        hello);
        /* [tableaux] */
        SET("tableaux",    "en_tete",      en_tete);
        SET("tableaux",    "indicatif",    indicatif);
        SET("tableaux",    "adresse",      adresse);
        SET("tableaux",    "port",         port);
        SET("tableaux",    "date",         date);
        /* [ax25] */
        SET("ax25",        "disconnected", ax25_disconnected);
        SET("ax25",        "conn_pending", ax25_conn_pending);
        SET("ax25",        "disc_pending", ax25_disc_pending);
        SET("ax25",        "connected",    ax25_connected);
        SET("ax25",        "recovery",     ax25_recovery);
        SET("ax25",        "unknown",      ax25_unknown);
        /* [rose] */
        SET("rose",        "connected",    rose_connected);
        SET("rose",        "idle",         rose_idle);
        /* [netrom] */
        SET("netrom",      "connected",    netrom_connected);
        SET("netrom",      "idle",         netrom_idle);
        /* [qualite] */
        SET("qualite",     "bonne",        qualite_bonne);
        SET("qualite",     "moyenne",      qualite_moyenne);
        SET("qualite",     "nulle",        qualite_nulle);
        /* [indicateurs] */
        SET("indicateurs", "oui",          ind_oui);
        SET("indicateurs", "non",          ind_non);

#undef SET
    }

    fclose(fp);
    return 1;
}
