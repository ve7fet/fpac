/*
 * lang.c -- language of the messages shown to the user by fpacnode.
 *
 * F6BVP 2026-09-19.  Every message is written in English in the source and
 * wrapped in T().  When the session language is French, T() returns the
 * translation found in the table below, or the English text if there is
 * none, so messages can be translated a few at a time.  The translation
 * must use exactly the same printf conversions, in the same order, as the
 * English text.
 *
 * The default language is, in this order:
 *   1. "Language = en | fr | auto" in fpac.conf (auto = next steps);
 *   2. the system language: LC_ALL, LC_MESSAGES, LANGUAGE, LANG in the
 *      environment, then /etc/default/locale and /etc/locale.conf (the
 *      daemons started by the boot scripts usually have no LANG at all);
 *   3. English.
 * A user can change it for the current session with the LAng command.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "node.h"

#define LANG_EN 0
#define LANG_FR 1

int UserLang = LANG_EN;

struct msg_pair
{
	const char *en;
	const char *fr;
};

static const struct msg_pair catalog[] =
{
	{ "already connected to %s",
	  "déjà connecté à %s" },
	{ "No such node",
	  "Nœud inconnu" },
	{ "Invalid AX.25 port : %s",
	  "Port AX.25 invalide : %s" },
	{ "Unknown host %s",
	  "Hôte inconnu : %s" },
	{ "Unknown service %s",
	  "Service inconnu : %s" },
	{ "Unsupported address family",
	  "Famille d'adresses non prise en charge" },
	{ "Trying %s%s...",
	  "Essai de connexion %s%s..." },
	{ "Trying (user port %s) %s... Type <RETURN> to abort",
	  "Essai (port utilisateur %s) %s... Tapez <RETURN> pour interrompre" },
	{ "Trying (heard on port %s) %s... Type <RETURN> to abort",
	  "Essai (entendu sur le port %s) %s... Tapez <RETURN> pour interrompre" },
	{ "Trying %s%s %s... Type <RETURN> to abort",
	  "Essai %s%s %s... Tapez <RETURN> pour interrompre" },
	{ "Trying %s%s... Type <RETURN> to abort",
	  "Essai %s%s... Tapez <RETURN> pour interrompre" },
	{ "*** Failure with %s",
	  "*** Échec de la connexion avec %s" },
	{ "*** Failure with %s%s",
	  "*** Échec de la connexion avec %s%s" },
	{ "Route used to %s : %s via %s",
	  "Route utilisée vers %s : %s via %s" },
	{ "*** No NetRom node %s (NetRom nodes with this callsign: %s)",
	  "*** Pas de nœud NetRom %s (nœuds NetRom avec cet indicatif : %s)" },
	{ "-- <Return> more, A abort -- ",
	  "-- <Entrée> pour continuer, A pour arrêter -- " },
	{ "To connect : C %s",
	  "Pour se connecter : C %s" },
	{ "Connect callsign of %s unknown (not in the White Pages)",
	  "Indicatif de connexion de %s inconnu (absent des White Pages)" },
	{ "No usable neighbour to %s",
	  "Aucun voisin utilisable vers %s" },
	{ "%s is this node",
	  "%s est ce nœud" },
	{ "*** Aborted",
	  "*** Interrompu" },
	{ "Aborted",
	  "Interrompu" },
	{ "link setup...",
	  "établissement de la liaison..." },
	{ "*** Connected to %s",
	  "*** Connecté à %s" },
	{ "*** Connected to %s (Escape: ~. )",
	  "*** Connecté à %s (Échap : ~. )" },
	{ "Connect %s. Usage : Connect port callsign",
	  "Connexion %s. Usage : Connect port indicatif" },
	{ "invalid AX.25 port callsign - %s",
	  "indicatif de port AX.25 invalide - %s" },
	{ "Error: No gateway for destination %s",
	  "Erreur : aucune passerelle pour la destination %s" },
	{ "*** Relaying via %s...",
	  "*** Relais via %s..." },
	{ "*** Route unavailable, retrying via %s...",
	  "*** Route indisponible, nouvel essai via %s..." },
	{ "*** Route unavailable, retrying via another neighbour...",
	  "*** Route indisponible, nouvel essai via un autre voisin..." },
	{ "*** White Pages entry for %s restored",
	  "*** Fiche White Pages de %s restaurée" },
	{ "*** Disconnected%s",
	  "*** Déconnecté%s" },
	{ "*** Reconnected to %s",
	  "*** Reconnecté à %s" },
	{ "*** No DNIC given, assuming the local DNIC : %s @ %s -- give the full address if the correspondent is on another network",
	  "*** Pas de DNIC indiqué, DNIC local supposé : %s @ %s -- indiquez l'adresse complète si le correspondant est sur un autre réseau" },
	{ "ROSE address DNIC : %s , %s - %d digits",
	  "Adresse ROSE DNIC : %s , %s - %d chiffres" },
	{ "Invalid ROSE address DNIC : %s , %s - %d digits",
	  "Adresse ROSE DNIC invalide : %s , %s - %d chiffres" },
	{ "ROSE address : %s via : %s",
	  "Adresse ROSE : %s via : %s" },
	{ "*** Invalid NetRom route (active route %d of %d): the kernel will not forward anything to %s",
	  "*** Route NetRom invalide (route active %d sur %d) : le noyau ne transmettra rien vers %s" },
	{ "*** NetRom direct (neighbour %s, route %d/%d, quality %d, obs %d)...",
	  "*** Relais NetRom direct (voisin %s, route %d/%d, qualité %d, obs %d)..." },
	{ "*** NetRom relaying via %s (route %d/%d, quality %d, obs %d)...",
	  "*** Relais NetRom via %s (route %d/%d, qualité %d, obs %d)..." },
	{ "*** Warning: AX.25 link to %s awaiting connection (attempt %d/%d, no answer)",
	  "*** Attention : liaison AX.25 vers %s en attente de connexion (essai %d/%d, aucune réponse)" },
	{ "%-16s   Invalid active route (%d of %d): the kernel forwards nothing\n",
	  "%-16s   Route active invalide (%d sur %d) : le noyau ne transmet rien\n" },
	{ "%s (Commands = ?) : ",
	  "%s (Commandes = ?) : " },
	{ NULL, NULL }
};

const char *T(const char *en)
{
	const struct msg_pair *m;

	if (UserLang != LANG_FR)
		return en;
	for (m = catalog; m->en != NULL; m++)
		if (strcmp(m->en, en) == 0)
			return m->fr;
	return en;
}

/* "fr_FR.UTF-8", "fr", "fr:en", "\"fr_CA\"" -> French ? */
static int lang_of(const char *s)
{
	while (*s == ' ' || *s == '"' || *s == '\'')
		s++;
	if ((s[0] == 'f' || s[0] == 'F') && (s[1] == 'r' || s[1] == 'R'))
		return LANG_FR;
	return LANG_EN;
}

static int is_unset(const char *v)
{
	return v == NULL || *v == '\0' || strcmp(v, "C") == 0 || strcmp(v, "POSIX") == 0;
}

/* Looks for LC_ALL/LC_MESSAGES/LANG in a shell-style file. -1 if none. */
static int lang_from_file(const char *path)
{
	static const char *keys[] = { "LC_ALL=", "LC_MESSAGES=", "LANGUAGE=", "LANG=", NULL };
	char line[256];
	FILE *fp;
	int k, res = -1;

	if ((fp = fopen(path, "r")) == NULL)
		return -1;
	for (k = 0; keys[k] != NULL && res < 0; k++)
	{
		rewind(fp);
		while (fgets(line, sizeof(line), fp) != NULL)
		{
			if (line[0] == '#')
				continue;
			if (strncmp(line, keys[k], strlen(keys[k])) == 0)
			{
				const char *v = line + strlen(keys[k]);
				if (!is_unset(v))
				{
					res = lang_of(v);
					break;
				}
			}
		}
	}
	fclose(fp);
	return res;
}

static int lang_system(void)
{
	static const char *env[] = { "LC_ALL", "LC_MESSAGES", "LANGUAGE", "LANG", NULL };
	int i, r;

	for (i = 0; env[i] != NULL; i++)
	{
		const char *v = getenv(env[i]);
		if (!is_unset(v))
			return lang_of(v);
	}
	if ((r = lang_from_file("/etc/default/locale")) >= 0)
		return r;
	if ((r = lang_from_file("/etc/locale.conf")) >= 0)
		return r;
	return LANG_EN;
}

void lang_init(void)
{
	const char *c = cfg.language;

	if (strcasecmp(c, "fr") == 0 || strcasecmp(c, "french") == 0 ||
	    strncasecmp(c, "fran", 4) == 0 || strcmp(c, "2") == 0)
		UserLang = LANG_FR;
	else if (strcasecmp(c, "en") == 0 || strncasecmp(c, "eng", 3) == 0 ||
		 strcmp(c, "1") == 0)
		UserLang = LANG_EN;
	else
		UserLang = lang_system();
}

/* LAng [1|2|en|fr] : choose the language of the current session. */
int do_lang(int argc, char **argv)
{
	if (argc >= 2)
	{
		const char *a = argv[1];

		if (strcmp(a, "1") == 0 || strcasecmp(a, "en") == 0 || strncasecmp(a, "eng", 3) == 0)
			UserLang = LANG_EN;
		else if (strcmp(a, "2") == 0 || strcasecmp(a, "fr") == 0 ||
			 strncasecmp(a, "fra", 3) == 0 || strncasecmp(a, "fre", 3) == 0)
			UserLang = LANG_FR;
		else
		{
			node_msg("Usage: lang <1|2>   1 English  2 Fran\xc3\xa7" "ais");
			return 0;
		}
	}
	node_msg("Language / Langue : %s", UserLang == LANG_FR ? "Fran\xc3\xa7" "ais" : "English");
	node_msg("1 English   2 Fran\xc3\xa7" "ais   (lang <1|2>)");
	return 0;
}
