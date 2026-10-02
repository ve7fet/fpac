/*
* node/gateway.c
*
* FPAC project
*
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <syslog.h>
#include <sys/file.h>
/*#include <sys/stat.h>*/
#include <sys/time.h>
#include <sys/ioctl.h>
#include <netdb.h>

#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include "node.h"
#include "io.h"
#include "wp.h"

#include "ax25compat.h"

#ifndef SOL_AX25
#define SOL_AX25 257
#endif

#ifndef AF_FLEXNET
#define AF_FLEXNET 128
#endif

#define ENOIOCTLCMD 515

extern int node_is_connected(char *);

struct route {
	char addr[11];
	int mask;
	int ndigi;
	char node[3][10];
	struct route *next;
};

struct route *head = NULL;

void free_routes(void)
{
	struct route *r;
	
	r = head;
	
	while (r)
	{
		head = r->next;
		free(r);
		r = head;
	}
}

/*Help for command Routes

USAGE
        Routes [Rose/Fpac address]

DESCRIPTION
        The Route Table shows the primary and alternate routes set for addresses.
	Note: 
	    Open = available 
	    Closed = unavailable at this time. 

*/
void read_routes(void)
{
	struct route *r;
	struct proc_rs_neigh *pv, *listv;
	struct proc_rs_nodes *pn, *listn;
	int loopback = -1;
	char *addr = NULL;

	if (head)
		free_routes();
		
	/* Routes */
	if ((listv = read_proc_rs_neigh ()) == NULL)
	{
		if (errno)
			fprintf(stderr, "do_routes: read_proc_rs_neigh %s\n", strerror(errno));
		return;
	}

	/* Search the node number of the loopback */
	for (pv = listv; pv != NULL; pv = pv->next)
		if (strncmp (pv->call, "RSLOOP", 6) == 0)
		{
			loopback = pv->addr;
			break;
		}

	if ((listn = read_proc_rs_nodes ()) == NULL)
	{
		if (errno)
			fprintf(stderr, "do_routes: read_proc_rs_nodes %s\n", strerror(errno));
		return;
	}

	for (pn = listn; pn != NULL; pn = pn->next)
	{
		if (pn->neigh1 == loopback)
			continue;

		if ((addr) && (strncmp (addr, pn->address, pn->mask) != 0))
			continue;

		if (pn->address[0] == '*')
			continue;

		r = calloc(sizeof(struct route), 1);
		if (r == NULL)
			break;
			
		r->next = head;
		head = r;
		
		strcpy(r->addr, pn->address);
		r->mask = pn->mask;
		r->ndigi = pn->n;

		for (pv = listv; pv != NULL; pv = pv->next)
			if (pn->neigh1 == pv->addr)
				strcpy(r->node[0], pv->call);
		for (pv = listv; pv != NULL; pv = pv->next)
			if (pn->neigh2 == pv->addr)
				strcpy(r->node[1], pv->call);
		for (pv = listv; pv != NULL; pv = pv->next)
			if (pn->neigh3 == pv->addr)
				strcpy(r->node[2], pv->call);
	}
	free_proc_rs_neigh (listv);
	free_proc_rs_nodes (listn);
}

/* Find the address of the adjacent node which routes to route*/
char *get_address(int fd, char *address)
{
	int nroute;
	int mask;
	struct route *r;
	struct route *f = NULL;
	static char retaddr[11];
	node_t *n;

	/* read the routing table */
	read_routes();

	/* find the route in the table */
	r = head;
	
	while (r)
	{
		for (mask=10; mask >=4; mask--)
		{
			f=NULL;
			if ((strncmp(r->addr, address, mask) == 0) && mask == r->mask)
				{
					f = r;
				}
		if (f)
			{
/* node_t * */		n = cfg.node;

		/* Search the address of the node in the configuration file */
			while (n)
			{
			for (nroute=0; nroute < f->ndigi; nroute++)
				{
				strtok(n->call, " \t,;");
				if (callsign_eq(f->node[nroute], n->call))
				{
		/* Only return address of a connected neighbour */
				if (node_is_connected(n->call))
					{
					strcpy(retaddr, n->dnic);
					strcat(retaddr, n->addr);
					return retaddr;
					}
				}
				}
				n = n->next;
				}
			}			
		}
		r = r->next;
	}
	return NULL;
}


static char *reason(unsigned char cause)
{
	static char *desc;

	switch (cause)
	{
	case ROSE_DTE_ORIGINATED:
		desc = "Remote Station cleared connection";
		break;
	case ROSE_NUMBER_BUSY:
		desc = "Remote Station is busy";
		break;
	case ROSE_INVALID_FACILITY:
		desc = "Invalid X.25 Facility Requested";
		break;
	case ROSE_NETWORK_CONGESTION:
		desc = "Network Congestion";
		break;
	case ROSE_OUT_OF_ORDER:
		desc = "Out of Order, link unavailable";
		break;
	case ROSE_ACCESS_BARRED:
		desc = "Access Barred";
		break;
	case ROSE_NOT_OBTAINABLE:
		desc = "No Route Opened for address specified";
		break;
	case ROSE_REMOTE_PROCEDURE:
		desc = "Remote Procedure Error";
		break;
	case ROSE_LOCAL_PROCEDURE:
		desc = "Local Procedure Error";
		break;
	case ROSE_SHIP_ABSENT:
		desc = "Remote Station not responding";
		break;
	default:
		desc = "Unknown";
		break;
	}

	return desc;
}

/*
 * Initiate a AX.25, NET/ROM, ROSE or TCP connection to the host
 * specified by `address'.
 * 
 * Help for command Connect

USAGE
        Connect <port> <call> [via <digi1> ...] [s|d]    For AX.25
        Connect <call>                                   For AX.25/WP/Mheard/Flexnet
        Connect <call | alias> [s|d]                     For NET/ROM
	Connect <call> <address> [<digi>] [d|s]          For ROSE

DESCRIPTION
        Initiates an AX.25, NET/ROM or ROSE connection to a remote
        host. If more than two parameters are entered and the
        second parameter is ten characters in length then it is
        interpreted as a ROSE connection, otherwise the first
        parameter is interpreted as a port name and AX.25 is used
        to make the connection via that port. If only one parameter
        is given the connection is made using NET/ROM, Flexgate 

        If the callsign is known by the WP database, you can only
        specify it, the routing will be automatic.

        If the callsign is unknown by the Wp database but heard by
        the mheardd demon, an AX25 connection is initiated on the
        last heard port.

        If the callsign is unknown by the Wp database but known in
        the Flexnet destinations table, an AX25 connection is initiated
        via the Flexgate.

        If a single `s' is entered as the last parameter, then when
        the remote host disconnects you will be returned to this node.
        If a single `d' is entered as the last parameter, you will
        be disconnected from this node too. Default behaviour (neither
        `s' nor `d' entered) depends on sysop configuration.

 */

/*
 * --- Failover ROSE sur repli generique multi-voisins (WP) ---
 *
 * Quand une adresse trouvee dans les White Pages n'a pas de route
 * specifique locale, fpad/fpacwpd s'appuient sur l'entree de repli
 * DNIC=0 declarant plusieurs voisins candidats (ex: "2080 = HubA HubB").
 * Le noyau (rose_get_neigh(), net/rose/rose_route.c) choisit alors le
 * PREMIER voisin dont le lien AX.25 est up, sans savoir s'il connait
 * reellement la destination au-dela. Si ce voisin renvoie un CLEAR
 * "route introuvable" (ROSE_NOT_OBTAINABLE / ROSE_OUT_OF_ORDER), rien
 * ne bascule automatiquement vers un autre candidat declare.
 *
 * ROSE_FAILOVER_MAX bornes le nombre d'essais. On ne retire jamais
 * plus d'entrees que de candidats reellement declares pour l'adresse
 * visee, et on restaure systematiquement (rose_restore_failover())
 * tout ce qui a ete retire, succes ou echec final, pour ne pas
 * degrader la table de routage partagee au-dela de la duree de la
 * tentative en cours.
 */
#define ROSE_FAILOVER_MAX 6

/*
 * Cherche, parmi les entrees /proc/net/rose_nodes dont le prefixe
 * correspond a addr10, le voisin que le noyau va (ou va maintenant)
 * essayer, en excluant ceux deja presents dans tried[].
 *
 * Reproduit EXACTEMENT rose_get_neigh() (net/rose/rose_route.c). Le noyau
 * trie deja rose_node_list par masque decroissant (le match le plus
 * specifique en tete) et parcourt les voisins dans l'ordre du tableau
 * (neigh1, neigh2, neigh3 de /proc/net/rose_nodes), en DEUX passages :
 *
 *   1) le premier voisin "restarted" (colonne restart == "yes" de
 *      /proc/net/rose_neigh, affiche "Opened" par la commande ro) ;
 *   2) seulement s'il n'y en a aucun, le premier voisin dont le
 *      temporisateur d'echec ftimer n'est pas en marche (colonne tf == 0).
 *      Un voisin "Closed" n'est donc tente qu'en dernier recours : le
 *      noyau etablit alors son lien AX.25 a la demande (SABM).
 *
 * Avant la 4.1.6-rc1 ce parcours prenait simplement le premier voisin
 * de la liste, sans regarder restart : le message "Relais via" nommait
 * alors un voisin "Closed" en tete de liste (ex. F6KKR-9) alors que le
 * noyau utilisait en realite un voisin "Opened" plus loin (F3KT-11 ou
 * F6BVP-9), et la bascule retirait la route du mauvais voisin.
 *
 * multi_only != 0 : ne considere que les entrees a plusieurs voisins
 * (le repli generique DNIC=0 declarant plusieurs candidats). C'est le
 * perimetre de la bascule, qui retire des routes (SIOCDELRT) : elle ne
 * doit jamais toucher a une route specifique a un seul voisin.
 * multi_only == 0 : lecture seule, toutes les entrees (prediction du
 * voisin reellement utilise par le noyau, pour le message a l'utilisateur).
 *
 * Retourne 0 et remplit *out si un candidat est trouve, -1 sinon
 * (plus aucun candidat non essaye pour cette adresse).
 */
static int rose_pick_failover(const char *addr10,
			       char tried[][10], int ntried,
			       struct rose_route_struct *out, int multi_only)
{
	struct proc_rs_nodes *nodes, *n;
	struct proc_rs_neigh *neighs, *ng;
	unsigned int cand[3];
	int ncand, i, j, skip, pass;

	/* kernel order (most specific first), see read_proc_rs_nodes_ordered() */
	if ((nodes = read_proc_rs_nodes_ordered()) == NULL)
		return -1;

	if ((neighs = read_proc_rs_neigh()) == NULL)
	{
		free_proc_rs_nodes(nodes);
		return -1;
	}

	for (pass = 0; pass < 2; pass++)
	{
		for (n = nodes; n; n = n->next)
		{
			if (multi_only && n->n < 2)
				continue;	/* pas un repli a plusieurs candidats */
			if (strncmp(n->address, addr10, n->mask) != 0)
				continue;

			ncand = 0;
			if (n->neigh1) cand[ncand++] = n->neigh1;
			if (n->neigh2) cand[ncand++] = n->neigh2;
			if (n->neigh3) cand[ncand++] = n->neigh3;

			for (i = 0; i < ncand; i++)
			{
				for (ng = neighs; ng; ng = ng->next)
				{
					if ((unsigned int) ng->addr != cand[i])
						continue;

					skip = 0;
					for (j = 0; j < ntried; j++)
						if (callsign_eq(tried[j], ng->call))
						{
							skip = 1;
							break;
						}
					if (skip)
						continue;

					/* passage 1 : voisin "restarted" seulement ;
					 * passage 2 : ftimer arrete seulement */
					if (pass == 0 && strcmp(ng->restart, "yes") != 0)
						continue;
					if (pass == 1 && ng->tf != 0)
						continue;

					memset(out, 0, sizeof(*out));
					rose_aton(n->address, out->address.rose_addr);
					out->mask = n->mask;
					strcpy(out->device, ng->dev);
					ax25_aton_entry(ng->call, out->neighbour.ax25_call);

					free_proc_rs_neigh(neighs);
					free_proc_rs_nodes(nodes);
					return 0;
				}
			}
		}
	}

	free_proc_rs_neigh(neighs);
	free_proc_rs_nodes(nodes);
	return -1;
}

/*
 * Read-only prediction for the "routes <address>" command: the route
 * entry and the neighbour the kernel will use to reach addr10, with
 * the same rule as rose_get_neigh(). Returns 0 and fills entry (10
 * digits), *mask and call, or -1 when no neighbour can be used.
 */
int rose_predict_route(const char *addr10, char *entry, int *mask, char *call)
{
	struct rose_route_struct r;

	if (rose_pick_failover(addr10, NULL, 0, &r, 0) != 0)
		return -1;
	strcpy(entry, rose_ntoa(&r.address));
	*mask = r.mask;
	strcpy(call, ax25_ntoa(&r.neighbour));
	return 0;
}

/*
 * Restaure dans la table de routage noyau toutes les entrees retirees
 * par rose_pick_failover()/SIOCDELRT pendant les essais successifs.
 * A appeler systematiquement en sortie, que la connexion ait fini par
 * reussir ou que tous les candidats aient ete epuises.
 */
static void rose_restore_failover(struct rose_route_struct *removed, int nremoved)
{
	int rs, k;

	if (nremoved <= 0)
		return;

	if ((rs = socket(AF_ROSE, SOCK_SEQPACKET, 0)) < 0)
		return;

	for (k = 0; k < nremoved; k++)
		ioctl(rs, SIOCADDRT, &removed[k]);

	close(rs);
}

/*
 * Tente une bascule vers le prochain voisin candidat declare pour
 * l'adresse addr10, si la cause d'echec le justifie et qu'il reste des
 * essais disponibles (ntried < ROSE_FAILOVER_MAX). Utilisee aussi bien
 * apres un echec immediat de connect_to() (le voisin choisi par le
 * noyau refuse tout de suite la connexion : cas le plus frequent) que,
 * comme avant, apres un CLEAR recu en cours de session.
 *
 * En cas de succes, retire la route du candidat fautif (SIOCDELRT),
 * l'enregistre dans removed[]/tried[] pour restauration/exclusion
 * ulterieures, et retourne 1 (l'appelant doit reessayer). Retourne 0
 * si aucune bascule n'est tentee (cause non pertinente, essais
 * epuises, ou plus aucun candidat disponible).
 */
static int rose_try_failover(int cause, const char *addr10,
			      char tried[][10], int *ntried,
			      struct rose_route_struct *removed, int *nremoved,
			      char *next_call)
{
	struct rose_route_struct cand, next_cand;
	int rs;

	if (cause != ROSE_NOT_OBTAINABLE && cause != ROSE_OUT_OF_ORDER)
		return 0;
	if (*ntried >= ROSE_FAILOVER_MAX)
		return 0;
	if (rose_pick_failover(addr10, tried, *ntried, &cand, 1) != 0)
		return 0;

	if ((rs = socket(AF_ROSE, SOCK_SEQPACKET, 0)) < 0)
		return 0;

	if (ioctl(rs, SIOCDELRT, &cand) == -1)
	{
		close(rs);
		return 0;
	}

	removed[(*nremoved)++] = cand;
	strcpy(tried[(*ntried)++], ax25_ntoa(&cand.neighbour));
	close(rs);

	/* Indicatif du voisin que le noyau va essayer au prochain retry,
	 * pour l'afficher a l'utilisateur (message plus informatif que
	 * "un autre voisin"). Chaine vide si aucun candidat restant. */
	if (next_call)
	{
		if (rose_pick_failover(addr10, tried, *ntried, &next_cand, 0) == 0)
			strcpy(next_call, ax25_ntoa(&next_cand.neighbour));
		else
			*next_call = '\0';
	}
	return 1;
}

/*
 * F6BVP 2026-09-19: informational only -- name the neighbour the kernel
 * will really use for a NetRom connection, as done for ROSE
 * ("*** Relais via ..."). Without it a connection to a NetRom node
 * (typed by callsign or by alias) gives no clue which route carries it,
 * so a failure cannot be told from a success through another route.
 *
 * Reproduces nr_route_frame() (net/netrom/nr_route.c): the frame goes to
 * nr_node->routes[nr_node->which].neighbour. /proc/net/nr_nodes prints
 * which + 1 in column "w" and the (up to 3) routes as quality/obs/neigh
 * triplets; /proc/net/nr_neigh maps the neighbour number to its callsign.
 * Read-only: nothing is modified.
 */
static void netrom_show_relay(const char *nodecall)
{
	struct proc_nr_nodes *nodes, *n;
	struct proc_nr_neigh *neighs, *ng;
	int addr, qual, obs;

	if ((nodes = read_proc_nr_nodes()) == NULL)
		return;

	for (n = nodes; n; n = n->next)
		if (callsign_eq(n->call, nodecall))
			break;

	if (n && n->n > 0 && (n->w < 1 || n->w > n->n))
	{
		/* "which" out of range: nr_route_frame() returns 0 and drops
		 * everything, so the connection will hang with no frame sent. */
		node_msg(T("*** Invalid NetRom route (active route %d of %d): the kernel will not forward anything to %s"),
			 n->w, n->n, n->call);
	}
	else if (n && n->n > 0 && n->w >= 1 && n->w <= n->n && n->w <= 3)
	{
		addr = (n->w == 1) ? n->addr1 : (n->w == 2) ? n->addr2 : n->addr3;
		qual = (n->w == 1) ? n->qual1 : (n->w == 2) ? n->qual2 : n->qual3;
		obs  = (n->w == 1) ? n->obs1  : (n->w == 2) ? n->obs2  : n->obs3;

		if ((neighs = read_proc_nr_neigh()) != NULL)
		{
			for (ng = neighs; ng; ng = ng->next)
				if (ng->addr == addr)
					break;

			if (ng)
			{
				if (callsign_eq(ng->call, n->call))
					node_msg(T("*** NetRom direct (neighbour %s, route %d/%d, quality %d, obs %d)..."),
						 ng->call, n->w, n->n, qual, obs);
				else
					node_msg(T("*** NetRom relaying via %s (route %d/%d, quality %d, obs %d)..."),
						 ng->call, n->w, n->n, qual, obs);
				{
					int rn2c = 0, rn2m = 0;

					/* SABM sent, no answer yet: the connection cannot
					 * succeed until the neighbour answers. */
					if (nr_link_pending(ng->call, &rn2c, &rn2m))
						node_msg(T("*** Warning: AX.25 link to %s awaiting connection (attempt %d/%d, no answer)"),
							 ng->call, rn2c, rn2m);
				}
			}
			free_proc_nr_neigh(neighs);
		}
	}
	free_proc_nr_nodes(nodes);
}

static int connect_to(char *address[], int family, int escape, char *source,
		       struct rose_cause_struct *out_cause)
{
	int i;
	int fd;
	int connected = 1;
	fd_set read_fdset;
	fd_set write_fdset;
	int addrlen;
	union
	{
		struct full_sockaddr_ax25 ax25;
		struct full_sockaddr_rose rose;
		struct sockaddr_in inet;
	}
	sockaddr;
	char call[10], path[20], origin[80], *dest, *cp, *eol, *addr;
	int ret;
	unsigned retlen = sizeof(int);
	int paclen;
	int pos;
	int explicit_dnic;
	char c;
	int nb;
	struct hostent *hp;
	struct servent *sp;
	struct proc_nr_nodes *np;
	struct rose_cause_struct rose_cause;
	struct rose_facilities_struct facilities;
	
	strcpy(call, User.call);

	memset(&facilities, 0x00, sizeof(struct rose_facilities_struct));
	memset(&rose_cause, 0x00, sizeof(struct rose_cause_struct));
	memset(&sockaddr.rose, 0x00, sizeof(struct full_sockaddr_rose));

	/*
	 * Fill in protocol specific stuff.
	 */
	switch (family)
	{
	case AF_ROSE:
		if (callsign_eq(address[0], cfg.alt_callsign))
		{
			node_msg(T("already connected to %s"), cfg.alt_callsign);
			return -1;
		}

		if ((fd = socket(AF_ROSE, SOCK_SEQPACKET, 0)) < 0)
		{
			node_perror("connect_to: socket", errno);
			return -1;
		}
		sockaddr.rose.srose_family = AF_ROSE;
		sockaddr.rose.srose_ndigis = 0;
		ax25_aton_entry(call, sockaddr.rose.srose_call.ax25_call);
		addr = rs_get_addr(NULL);

		if (addr == NULL)
		{
			node_perror("connect_to: no rose address", errno);
			close(fd);
			return -1;
		}
		rose_aton(addr, sockaddr.rose.srose_addr.rose_addr);
		addrlen = sizeof(struct full_sockaddr_rose);

		if (bind(fd, (struct sockaddr *) &sockaddr, addrlen) == -1)
		{
			node_perror("connect_to: bind", errno);
			close(fd);
			return -1;
		}

		pos = 1;
		memset(path, 0, sizeof(path));
		explicit_dnic = 0;

		/* Default DNIC */
		memcpy(path, rs_get_addr(NULL), 4);

		addrlen = strlen(address[pos]);

		if (addrlen == 3)
		{
			/* Country designator, given as its own argument */
			memcpy(path, des2dnic(address[pos]), 4);
			explicit_dnic = 1;
			++pos;
			addrlen = strlen(address[pos]);
		}
		else if (addrlen == 4)
		{
			/* DNIC, given as its own argument */
			memcpy(path, address[pos], 4);
			explicit_dnic = 1;
			++pos;
			addrlen = strlen(address[pos]);
		}
// DEBUG F6BVP
//		node_msg(T("ROSE address DNIC : %s , %s - %d digits"), path, address[pos], addrlen);

		if ((addrlen != 6) && (addrlen != 10))
		{
			node_msg(T("Invalid ROSE address DNIC : %s , %s - %d digits"), path, address[pos], addrlen);
			return (-1);
		}

		memcpy(path + (10 - addrlen), address[pos], addrlen);

		/*
		 * F6BVP 2026-09-16: a 6-digit address with no explicit DNIC
		 * (either as its own 3/4-digit argument, or embedded in a
		 * full 10-digit address) silently defaults to THIS node's own
		 * DNIC (rs_get_addr() above), not to whatever network the
		 * destination actually lives on. On a node whose own DNIC
		 * differs from the target's (e.g. F4KLO-9 is DNIC 2090, but
		 * f6bvp-8/f6bvp-10/f6bvp-12 are DNIC 2080), this silently
		 * builds a wrong address that still finds *a* route (the
		 * DNIC=0 generic fallback) and can trigger a routing loop on
		 * the far end instead of a clean, obvious failure. Root cause
		 * of the F3KT/f6bvp-8 ROSE storm of 2026-09-16. Warn so the
		 * sysop notices immediately and can abort/retype with the
		 * full address instead of the connection silently going to
		 * the wrong network.
		 */
		if (!explicit_dnic && addrlen == 6)
			node_msg(T("*** No DNIC given, assuming the local DNIC : %s @ %s -- give the full address if the correspondent is on another network"), strupr(address[0]), roseaddr(path));

		sprintf(User.dl_name, "%s @ %s", strupr(address[0]), roseaddr(path));

		++pos;

		for (i = pos; address[i]; i++)
		{
			strcat(User.dl_name, " ");
			strcat(User.dl_name, strupr(address[i]));
		}
		sockaddr.rose.srose_family = AF_ROSE;
		sockaddr.rose.srose_ndigis = 0;
		if (ax25_aton_entry(address[0], sockaddr.rose.srose_call.ax25_call) ==
			-1)
		{
			close(fd);
			return -1;
		}
		if (rose_aton(path, sockaddr.rose.srose_addr.rose_addr) == -1)
		{
			close(fd);
			return -1;
		}
		for (i = 0; address[i + pos]; i++)
		{
			if (i == ROSE_MAX_DIGIS)
				break;
	

			if (ax25_aton_entry
				(address[i + pos],
				 sockaddr.rose.srose_digis[i].ax25_call) == -1)
			{
				close(fd);
				return -1;
			}

			++sockaddr.rose.srose_ndigis;
		}
		
		addrlen = sizeof(struct full_sockaddr_rose);
		
// DEBUG F6BVP
//		node_msg(T("ROSE address : %s via : %s"), path, address[pos]);
//		node_msg("Connnect_to() digi %s", ax25_ntoa(&sockaddr.rose.srose_digis[0].ax25_call));
//				
		paclen = rs_config_get_paclen(NULL); 
		eol = ROSE_EOL;
		break;

	case AF_NETROM:
		if (callsign_eq(address[0], cfg.alt_callsign))
		{
			node_msg(T("already connected to %s"), cfg.alt_callsign);
			return -1;
		}

		if ((fd = socket(AF_NETROM, SOCK_SEQPACKET, 0)) < 0)
		{
			node_perror("connect_to: socket", errno);
			return -1;
		}
		/* Why on earth is this different from ax.25 ????? */
		sprintf(path, "%s", nr_config_get_addr(NrPort));
		ax25_aton(path, &sockaddr.ax25);
		sockaddr.ax25.fsa_ax25.sax25_family = AF_NETROM;
		addrlen = sizeof(struct full_sockaddr_ax25);

		if (bind(fd, (struct sockaddr *) &sockaddr, addrlen) == -1)
		{
			node_perror("connect_to: bind", errno);
			close(fd);
			return -1;
		}
		if ((np = find_node(address[0], NULL)) == NULL)
		{
			node_msg(T("No such node"));
			return -1;
		}
		strcpy(User.dl_name, print_node(np->alias, np->call));
		if (ax25_aton(np->call, &sockaddr.ax25) == -1)
		{
			close(fd);
			return -1;
		}
		sockaddr.ax25.fsa_ax25.sax25_family = AF_NETROM;
		addrlen = sizeof(struct sockaddr_ax25);
		paclen = nr_config_get_paclen(NrPort);
		eol = NETROM_EOL;
		break;

	case AF_AX25:
	case AF_FLEXNET:

          if (family == AF_FLEXNET)
                dest = address[0];
            else 
/*FSA       AF_NETROM needs to adjust port 'cause there's a device like ax2 * /
            dest=address[0];
            address[0]=ax25_config_get_name(address[0]);
FSA*/
                if ((dest = ax25_config_get_addr(address[0])) == NULL) {
                    node_msg(T("Invalid AX.25 port : %s"), address[0]);
                    return -1;
                }

		if (callsign_eq(address[0], cfg.alt_callsign))
		{
			node_msg(T("already connected to %s"), call);
			return -1;
		}

		if ((fd = socket(AF_AX25, SOCK_SEQPACKET, 0)) < 0)
		{
			node_perror("connect_to: socket", errno);
			return -1;
		}

		sprintf(path, "%s %s", call, dest);

		ax25_aton(path, &sockaddr.ax25);
		sockaddr.ax25.fsa_ax25.sax25_family = AF_AX25;
		addrlen = sizeof(struct full_sockaddr_ax25);
		if (bind(fd, (struct sockaddr *) &sockaddr, addrlen) == -1)
		{
			node_perror("connect_to: bind", errno);
			close(fd);
			return -1;
		}
		if (ax25_aton_arglist((const char **) &address[1], &sockaddr.ax25) ==
			-1)
		{
			close(fd);
			return -1;
		}

#ifndef AX25_HBIT
#define      AX25_HBIT 0x80
#endif
		/* Insert the alternate callsign as first digi */
		for (i = sockaddr.ax25.fsa_ax25.sax25_ndigis; i > 0; i--)
		{
			sockaddr.ax25.fsa_digipeater[i] =
				sockaddr.ax25.fsa_digipeater[i - 1];
		}
		if (ax25_aton_entry
			(cfg.alt_callsign,
			 sockaddr.ax25.fsa_digipeater[0].ax25_call) == -1)
		{
			close(fd);
			return (-1);
		}
		sockaddr.ax25.fsa_digipeater[0].ax25_call[6] |= AX25_HBIT;
		++sockaddr.ax25.fsa_ax25.sax25_ndigis;

		if(address[1] != NULL)	strcpy(User.dl_name, strupr(address[1]));
		strcpy(User.dl_port, strupr(address[0]));
		sockaddr.ax25.fsa_ax25.sax25_family = AF_AX25;
		addrlen = sizeof(struct full_sockaddr_ax25);
		paclen = ax25_config_get_paclen(address[0]);

		i = 1;
		if (setsockopt(fd, SOL_AX25, AX25_IAMDIGI, &i, sizeof(i)) == -1)
		{
			close(fd);
			return (-1);
		}
		eol = AX25_EOL;
		break;
	case AF_INET:
		if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		{
			node_perror("connect_to: socket", errno);
			return -1;
		}
		if ((hp = gethostbyname(address[0])) == NULL)
		{
			node_msg(T("Unknown host %s"), address[0]);
			close(fd);
			return -1;
		}
		sockaddr.inet.sin_family = AF_INET;
		sockaddr.inet.sin_addr.s_addr =
			((struct in_addr *) (hp->h_addr))->s_addr;
		sp = NULL;
		if (address[1] == NULL)
			sp = getservbyname("telnet", "tcp");
		if (sp == NULL)
			sp = getservbyname(address[1], "tcp");
		if (sp == NULL)
			sp = getservbyport(htons(atoi(address[1])), "tcp");
		if (sp != NULL)
		{
			sockaddr.inet.sin_port = sp->s_port;
		}
		else if (atoi(address[1]) != 0)
		{
			sockaddr.inet.sin_port = htons(atoi(address[1]));
		}
		else
		{
			node_msg(T("Unknown service %s"), address[1]);
			close(fd);
			return -1;
		}
		strcpy(User.dl_name, inet_ntoa(sockaddr.inet.sin_addr));
		if (sp != NULL)
			strcpy(User.dl_port, sp->s_name);
		else
			sprintf(User.dl_port, "%d", ntohs(sockaddr.inet.sin_port));
		addrlen = sizeof(struct sockaddr_in);
		paclen = 1024;
		eol = INET_EOL;
		break;
	default:
		node_msg(T("Unsupported address family"));
		return -1;
	}
	if (family == AF_FLEXNET)
		node_msg(T("Trying %s%s..."), (source) ? source : "", User.dl_name);
	else if (family == AF_AX25)
	{
		/* F6BVP 2026-09-16: name the port (User.dl_port, set a few
		 * lines above for this family) alongside the callsign --
		 * "(user port)" alone gave no clue which AX.25 port was
		 * actually being tried. Bernard 2026-09-16: put it inside the
		 * existing "(user port)" parenthesis rather than after it. */
		if (source && strcmp(source, "(user port) ") == 0)
			node_msg(T("Trying (user port %s) %s... Type <RETURN> to abort"), User.dl_port, User.dl_name);
		else if (source && strcmp(source, "(heard) ") == 0)
			node_msg(T("Trying (heard on port %s) %s... Type <RETURN> to abort"), User.dl_port, User.dl_name);
		else
			node_msg(T("Trying %s%s %s... Type <RETURN> to abort"), (source) ? source : "", User.dl_port, User.dl_name);
	}
	else
		node_msg(T("Trying %s%s... Type <RETURN> to abort"), (source) ? source : "", User.dl_name);
	usflush(User.fd);
	/*
	 * Ok. Now set up a non-blocking connect...
	 */
	if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
	{
		node_perror("connect_to: fcntl - fd", errno);
		close(fd);
		return -1;
	}
	if (fcntl(User.fd, F_SETFL, O_NONBLOCK) == -1)
	{
		node_perror("connect_to: fcntl - stdin", errno);
		close(fd);
		return -1;
	}
	if (connect(fd, (struct sockaddr *) &sockaddr, addrlen) == -1
		&& errno != EINPROGRESS)
	{
		if (family == AF_ROSE)
		{
			switch (errno)
			{
			case ENETUNREACH:
				ioctl(fd, SIOCRSGCAUSE, &rose_cause);
				break;
			case EISCONN:
				rose_cause.cause = ROSE_NUMBER_BUSY;
				rose_cause.diagnostic = 0x48;
				break;
			case EINVAL:
			default:
				rose_cause.cause = ROSE_LOCAL_PROCEDURE;
				rose_cause.diagnostic = 0;
				break;
			}
			node_msg(T("*** Failure with %s"), User.dl_name);
			node_msg("*** %s", reason(rose_cause.cause));
			if (out_cause)
				*out_cause = rose_cause;
		}
		else
		{
			node_msg(T("*** Failure with %s"), User.dl_name);
			node_msg("*** %s", strerror(errno));
		}
		close(fd);
		return -1;
	}
	User.dl_type = family;
	User.state = STATE_TRYING;
	update_user();

	/*
	 * ... and wait for it to finish (or user to abort).
	 */
	while (1)
	{
		FD_ZERO(&read_fdset);
		FD_ZERO(&write_fdset);
		FD_SET(fd, &write_fdset);
		FD_SET(User.fd, &read_fdset);
		if (select(fd + 1, &read_fdset, &write_fdset, 0, 0) == -1)
		{
			node_perror("connect_to: select", errno);
			break;
		}
		if (FD_ISSET(fd, &write_fdset))
		{
			nb = read(fd, &c, 0);

			if (nb == -1)
			{
				connected = 0;
			}

			/* See if we got connected or if this was an error */
			getsockopt(fd, SOL_SOCKET, SO_ERROR, &ret, &retlen);
			if (ret != 0)
			{
				if ((family == AF_AX25) || (family == AF_NETROM))
				{
					cp = strdup(strerror(ret));
					strlwr(cp);
					node_msg(T("*** Failure with %s"), User.dl_name);
					node_msg("*** %s", cp);
					fpaclog(LOGLVL_GW, "Failure with %s: %s", User.dl_name, cp);
					free(cp);
				}
				else
				{
/* DEBUG F6BVP 			i = ioctl(fd, SIOCRSGFACILITIES, &facilities); 
					syslog(LOG_INFO,"\nSystem facilities returned %d\n", i);
					fprintf(stderr, "\nSystem facilities returned %d\n", i);
					syslog(LOG_ERR, "\nSystem facilities returned %d\n", i);
*/				
					/* Get the cause and diag */
					rose_cause.cause = ROSE_LOCAL_PROCEDURE;
					rose_cause.diagnostic = 0;
					ioctl(fd, SIOCRSGCAUSE, &rose_cause);
					cp = strdup(reason(rose_cause.cause));
					origin[0] = '\0';
					if ((ioctl(fd, SIOCRSGFACILITIES, &facilities) != -1) &&
						(facilities.fail_call.ax25_call[0]))
					{
						sprintf(origin, " at %s @ %s",
								ax25_ntoa(&facilities.fail_call),
								fpac2asc(&facilities.fail_addr));
					}
					else
	/*				node_msg("facilities error %d %s\n", errno, strerror(errno));*/
					node_msg(T("*** Failure with %s%s"), User.dl_name, origin);
					node_msg("*** %s", cp);
					free(cp);
					if (out_cause)
						*out_cause = rose_cause;

/*				node_msg("fail call : %s at adresse %s\n", ax25_ntoa(&facilities.fail_call), fpac2asc(&facilities.fail_addr)); 					*/
				}
				close(fd);
				return -1;
			}
			break;
		}
		if (FD_ISSET(User.fd, &read_fdset))
		{
			if (readline(User.fd) != NULL)
			{
				node_msg(T("*** Aborted"));
				close(fd);
				return -1;
			}
			else if (errno != EAGAIN)
			{
				close(fd);
				return -1;
			}
		}
	}

	/*
	   * Connected .... 
	   * update wp only if connection is not
	   * using a known node as digipeater.
	 */

	if (connected)
	{
		if (family == AF_FLEXNET)
			{
			node_msg(T("link setup..."));
			}
		else
			{
			if (escape == -1)
				node_msg(T("*** Connected to %s"), User.dl_name);
			else
				node_msg(T("*** Connected to %s (Escape: ~. )"), User.dl_name);
			}
		usflush(User.fd);
		fpaclog(LOGLVL_GW, "Connected to %s", User.dl_name);
	}
	if (init_io(fd, paclen, eol) == -1)
	{
		node_perror("connect_to: Initializing I/O failed", errno);
		close(fd);
		return -1;
	}

	/* If EOL-conventions are compatible switch to binary mode */
	if (family == User.ul_type ||
		(family == AF_AX25 && User.ul_type == AF_NETROM) ||
		(family == AF_AX25 && User.ul_type == AF_ROSE) ||
		(family == AF_NETROM && User.ul_type == AF_AX25) ||
		(family == AF_NETROM && User.ul_type == AF_ROSE) ||
		(family == AF_ROSE && User.ul_type == AF_AX25) ||
		(family == AF_ROSE && User.ul_type == AF_NETROM))
	{
		set_eolmode(fd, EOLMODE_BINARY);
		set_eolmode(User.fd, EOLMODE_BINARY);
	}
	if (family == AF_INET)
		set_telnetmode(fd, 1);
	User.state = STATE_CONNECTED;
	update_user();
	return fd;
}

int ReConnectTo = TRUE;
long ConnTimeout = 900L;

int is_alias(char *callsign, alias_t * alias)
{
	alias_t *a = NULL;

	for (a = cfg.alias; (a != NULL); a = a->next)
	{
		if (callsign_eq(callsign, a->alias))	/* F3KT == F3KT-0 */
		{
			*alias = *a;
			return (1);
		}
	}
	return (0);
}

static int is_netrom(char *call, char *netrom_call)
{
	struct proc_nr_nodes *p, *list;
	int ret = 0;

	if ((list = read_proc_nr_nodes()) == NULL)
		return ret;

	/*
	 * F6BVP 2026-09-30: exact match only, on the callsign (F6BVP is
	 * F6BVP-0) or on the alias. The callsign used to be matched on its
	 * PREFIX: "c f6bvp-1" (the BBS, found in the White Pages) connected
	 * to the NetRom node F6BVP-12 instead, since NetRom is tried before
	 * the White Pages.
	 */
	for (p = list; p != NULL; p = p->next)
	{
		if (callsign_eq(p->call, call) || (strcasecmp(p->alias, call) == 0))
		{
			strcpy(netrom_call, p->call);
			ret = 1;
			break;
		}
	}

	free_proc_nr_nodes(list);

	return ret;
}

/*
 * Help: list the NetRom nodes with the same base callsign and another SSID.
 * F6BVP 2026-10-02: only shown when the callsign was found nowhere (it used
 * to be printed before a successful White Pages / ROSE connection, which
 * read as a failed NetRom attempt).
 */
static void netrom_near_help(char *call)
{
	struct proc_nr_nodes *p, *list;
	char near[160];
	size_t base;

	if ((list = read_proc_nr_nodes()) == NULL)
		return;

	near[0] = '\0';
	base = strcspn(call, "-");
	for (p = list; p != NULL; p = p->next)
	{
		if (strcspn(p->call, "-") == base &&
		    strncasecmp(p->call, call, base) == 0 &&
		    strlen(near) + strlen(p->call) + 2 < sizeof(near))
		{
			if (near[0])
				strcat(near, " ");
			strcat(near, p->call);
		}
	}
	if (near[0])
		node_msg(T("*** No NetRom node %s (NetRom nodes with this callsign: %s)"), call, near);

	free_proc_nr_nodes(list);
}

int is_wp(char *callsign, struct full_sockaddr_rose *wpaddr)
{
	ax25_address addr;
	wp_t wp;
	int ret;

	ax25_aton_entry(callsign, addr.ax25_call);

	ret = wp_get(&addr, &wp);

	if (ret == 0 && wp.is_deleted == 0)
	{
		*wpaddr = wp.address;
		return (1);
	}
	return (0);
}

/* Same, but only for a node record: a FPAC node is reached by ROSE */
static int is_wp_node(char *callsign, struct full_sockaddr_rose *wpaddr)
{
	ax25_address addr;
	wp_t wp;

	if (ax25_aton_entry(callsign, addr.ax25_call) == -1)
		return 0;

	if (wp_get(&addr, &wp) == 0 && wp.is_deleted == 0 && wp.is_node)
	{
		*wpaddr = wp.address;
		return 1;
	}
	return 0;
}

/* Initiate a connexion to destination 
 * via optional port specification
 * using protocol according to callsign SSID
 * or ROSE address
 * */

int do_connect(int argc, char **argv)
{
	struct flex_dst *flx;
	struct flex_gt *flgt;
	int fd, c, stay, escape;
	int n, cpt, k;
	int family = -1;
	fd_set fdset;
	char *connstr = NULL;
	alias_t alias;
	struct full_sockaddr_rose wpaddr;
	struct rose_facilities_struct facilities;
	struct rose_cause_struct rose_cause;
	char roseroute[11];
	char rosedigi[ROSE_MAX_DIGIS][10];
	char netromcall[10];
	char destaddr[11];
	char origin[80];
	char rsaddr[11];
	char *source;
	wp_t wpt;
	ax25_address ax25;
	/* F6BVP 2026-09-15: set when the user dials a ROSE address
	 * explicitly (numeric DNIC/address, not resolved via WP) -- see
	 * the "ROSE connections" branch below and its use after a
	 * successful connect_to(). */
	char explicit_rose_call[12] = "";
	char **argvp;
	char **argvp_base = NULL;
	int default_port = 0;
	int wp_opened = 0;
	/* F6BVP 2026-09-16: "via" is stripped below with no trace left, so
	 * "c CALL via DIGI" (one argument before "via") ends up with the
	 * exact same argv shape as "c PORT CALL" or a numeric ROSE
	 * address+digi -- it was being misrouted into the "ROSE
	 * connections" branch (or straight to the AX25 default case) with
	 * CALL wrongly read as a port name ("Port AX.25 invalide : CALL").
	 * via_used/argc_before_via remember how many real arguments came
	 * before "via" so the WP/NetRom/Flexnet/mheard/default-port
	 * dispatch below can use that instead of the post-strip argc. */
	int via_used = 0;
	int argc_before_via = 0;
	int eff_argc;

	argvp = calloc(10, sizeof(*argvp));
	argvp_base = argvp;
	for (n=0 ; n< 3; n++)
		argvp[n] = calloc(10, sizeof(**argvp));
	source = NULL;

	/* Delete the "v" or "via" */
	for (cpt = 0, n = 0; n < argc; n++)
	{
		if (!via_used && (strcasecmp(argv[n], "v") == 0 || strcasecmp(argv[n], "via") == 0))
		{
			via_used = 1;
			argc_before_via = cpt;
		}
		argv[cpt] = argv[n];
		if (strcasecmp(argv[n], "v") && strcasecmp(argv[n], "via"))
			++cpt;
	}
	argv[cpt] = NULL;
	argc = cpt;
	eff_argc = via_used ? argc_before_via : argc;

	stay = ReConnectTo;
	if (!strcasecmp(argv[argc - 1], "s"))
	{
		stay = 1;
		argv[--argc] = NULL;
	}
	else if (!strcasecmp(argv[argc - 1], "d"))
	{
		stay = 0;
		argv[--argc] = NULL;
	}
	if (argc < 2)
	{
		if (*argv[0] == 't')
			node_msg("Usage: telnet <host> [<port>] [d|s]");
		else
			node_msg
				("Usage: connect [<port>] <call> [via <call1> ...] [d|s]");
		goto done;
	}

	/* Telnet connection */
	if (*argv[0] == 't')
	{
		family = AF_INET;
	}

	else
	{
		wp_open("NODE");
		wp_opened = 1;
	/* Check FPAC Aliases */

		if (is_alias(argv[1], &alias))
		{
			/* F6BVP 2026-09-15: show what the alias actually expands to
			 * before parse_args() splits alias.path into argv[] in
			 * place (it null-terminates each token, so alias.path
			 * itself is only good for this until then). */
			node_msg("*** Alias %s -> %s", argv[1], alias.path);
			argc = parse_args(argv + 1, alias.path) + 1;
		}

		/* Check if its is a known port */
		if (ax25_config_get_addr(argv[1]))
		{
			if (callsign_eq(argv[1], cfg.alt_callsign))
			{
				node_msg(T("already connected to %s"), cfg.alt_callsign);
				goto done;
			}
 			if (argc < 3)
 			{
				node_msg(T("Connect %s. Usage : Connect port callsign"),argv[1]);
				goto done;
			}
			family = AF_AX25;
			source = "(user port) ";
		}

		/*
		 * F6BVP 2026-10-02: a FPAC node known in the White Pages is
		 * reached by ROSE, even when a NetRom node has the same
		 * callsign ("c f6bvp-12" went NetRom because the node uses
		 * its own callsign for NetRom). The NetRom alias and the
		 * nodes absent from the White Pages still go NetRom.
		 */
		else if ((eff_argc == 2) && (is_wp_node(argv[1], &wpaddr)))
		{
			strcpy(roseroute, rose_ntoa(&wpaddr.srose_addr));

			argv[2] = roseroute;
			argc = 3;
			for (n = wpaddr.srose_ndigis - 1; n >= 0; n--)
			{
				strcpy(rosedigi[n], ax25_ntoa(&wpaddr.srose_digis[n]));
				argv[argc++] = rosedigi[n];
			}
			argv[argc] = NULL;
			family = AF_ROSE;
			source = "(fpac wp) ";
		}

		/* Check if known NetRom node */
		else if ((eff_argc == 2) && (is_netrom(argv[1], netromcall)))
		{
			/* F6BVP 2026-09-19: say so when an alias resolved to a node
			 * callsign (exact match since 4.1.6-rc4). */
			if (!callsign_eq(argv[1], netromcall))
				node_msg("*** NetRom %s -> %s", argv[1], netromcall);
			argv[1] = netromcall;
			family = AF_NETROM;
			source = "(netrom node) ";
		}

		/* Check if in FPAC WP */
		else if ((eff_argc == 2) && (is_wp(argv[1], &wpaddr)))
		{
			strcpy(roseroute, rose_ntoa(&wpaddr.srose_addr));

			argv[2] = roseroute;
			argc = 3;
			for (n = wpaddr.srose_ndigis - 1; n >= 0; n--)
			{
				strcpy(rosedigi[n], ax25_ntoa(&wpaddr.srose_digis[n]));
				argv[argc++] = rosedigi[n];
			}
			argv[argc] = NULL;
			family = AF_ROSE;
			source = "(fpac wp) ";
		}

		/* ROSE connections */
		else if (eff_argc > 2)
		{
			if ((strlen(argv[2]) == 3) && (des2dnic(argv[2]) != NULL))
			{
				/* Digi is a country designator */
				strcpy(argv[2], des2dnic(argv[2]));
				family = AF_ROSE;
				strncpy(explicit_rose_call, argv[1], sizeof(explicit_rose_call) - 1);
			}
			else if (strspn(argv[2], "0123456789") == strlen(argv[2]))
			{
				/* Digi is a DNIC */
				family = AF_ROSE;
				strncpy(explicit_rose_call, argv[1], sizeof(explicit_rose_call) - 1);
			}
			else
			{
				if (ax25_aton_entry(argv[2], ax25.ax25_call) == -1)
				{
					node_msg(T("invalid AX.25 port callsign - %s"), argv[2]);
					goto done;
				}

				/* Check if it is a node callsign from wp */
				if ((wp_get(&ax25, &wpt) == 0) && (wpt.is_node))
				{
					strcpy(rsaddr, rose_ntoa(&wpt.address.srose_addr));
					argv[2] = rsaddr;
					family = AF_ROSE;
					source = "(fpac node) ";
				}

				/* Default to AX25 */
				else
				{
					family = AF_AX25;
				}
			}
		}
		/* Check if known Flex destination */
		else if ((eff_argc == 2) && ((flx = find_dest(argv[1], NULL)) != NULL))
		{						/* Check FlexNet */
			k = 1;
			strcpy(netromcall, argv[1]);
			flgt = find_gateway(flx->addr, NULL);
			if (flgt == NULL)
			{
				node_msg (T("Error: No gateway for destination %s"), netromcall);
				goto done;
			}

/*			argv[k++] = ax25_config_get_name(flgt->dev); */
			argv[k++] = flgt->dev;
			argv[k++] = netromcall;
		while ((k - 3) < AX25_MAX_DIGIS && flgt->digis[k - 3][0] != '\0')
			{
				k++;
				argv[k-1] = flgt->digis[(k) - 3];
			}
		if (strspn(argv[k-1], "0123456789") == strlen(argv[k-1])) {
			strcpy(destaddr, argv[k-1]);
			argv[k-1] = flgt->call;
			strcpy(argv[k], destaddr); 	
			k++;
		}
		else {
			argv[k++] = flgt->call;
		}
			argv[k++] = NULL;
			argc = k;
			source = "(flex) ";

	/* set protocole family */
		family = flgt->af_mode;

		}
		/* Try mheard -- F6BVP 2026-09-16: skip when the user gave an
		 * explicit "via DIGI", since is_heard() (lib/procutils.c)
		 * unconditionally OVERWRITES argv[2+] with whatever digipeater
		 * path was recorded in the mheard log for that station's last
		 * heard packet, silently discarding the digi the user just
		 * typed. An explicit via should win over mheard's guess. */
		else if (!via_used && is_heard(argv + 1))
		{
			family = AF_AX25;
			source = "(heard) ";
		}
		else
		{
			/* Fallback : connexion via le port par défaut de la configuration */
			if (eff_argc == 2)
				netrom_near_help(argv[1]);
			strcpy(argvp[0], argv[0]);
			strcpy(argvp[1], cfg.def_port);
			strcpy(argvp[2], argv[1]);

			/* F6BVP 2026-09-16: carry over any explicit "via DIGI..."
			 * digipeaters instead of silently dropping them -- see
			 * via_used/eff_argc above. argvp has 10 pointer slots
			 * (calloc'd, so already NULL past index 2); only the
			 * first 3 own their own fixed buffers, the rest just
			 * alias the original argv strings. */
			if (via_used)
			{
				int ai, di = 3;
				for (ai = eff_argc; ai < argc && di < 9; ai++, di++)
					argvp[di] = argv[ai];
				argvp[di] = NULL;
			}

			family = AF_AX25;
			source = "(user port) ";
			default_port = 1;
		}
		wp_opened = 0;
		wp_close();
	}

	if (family == AF_INET && argc > 3)
		connstr = argv[3];
	/* escape = (check_perms(PERM_NOESC, 0L) == 0) ? -1 : EscChar; */
	escape = 1;

	if (default_port == 0)
		++argv;
	else
		++argvp;

	{
	/* Failover uniquement pour une adresse ROSE resolue via les White
	 * Pages (ou l'alias node WP) : jamais pour une connexion ROSE
	 * explicitement tapee par l'utilisateur avec sa propre digi/route. */
	int rose_wp_failover = (family == AF_ROSE) && (source != NULL) &&
		(strcmp(source, "(fpac wp) ") == 0 ||
		 strcmp(source, "(fpac node) ") == 0);
	char rose_addr10[11];
	char rose_tried[ROSE_FAILOVER_MAX][10];
	int rose_ntried = 0;
	struct rose_route_struct rose_removed[ROSE_FAILOVER_MAX];
	int rose_nremoved = 0;
	struct rose_cause_struct conn_cause;
	char rose_next_call[10];

	/* F6BVP 2026-09-15: informational only, shown for every ROSE
	 * connection (previously only the WP-failover-enabled ones) -- an
	 * explicitly dialed address (e.g. "c F3KT-10 444501") gives no clue
	 * which neighbour the kernel is about to try, which can look like a
	 * silent hang when that neighbour's AX.25 link isn't already up
	 * (established on demand, can take a moment). rose_wp_failover
	 * itself is untouched: an explicit dial still never auto-retries
	 * through an alternate neighbour on failure, only WP-resolved ones
	 * do -- this only adds the "about to try" message. */
	if (family == AF_ROSE)
	{
		struct rose_route_struct first_cand;

		strncpy(rose_addr10, argv[1], 10);
		rose_addr10[10] = '\0';

		/* Indicatif du voisin par lequel le noyau va relayer le tout
		 * premier essai (avant tout echec/bascule) : simple lecture,
		 * rose_pick_failover() ne modifie pas la table de routage. */
		if (rose_pick_failover(rose_addr10, rose_tried, rose_ntried, &first_cand, 0) == 0 &&
		    strncmp(ax25_ntoa(&first_cand.neighbour), "RSLOOP", 6) != 0)
			node_msg(T("*** Relaying via %s..."), ax25_ntoa(&first_cand.neighbour));
	}
	else if (family == AF_NETROM)
		netrom_show_relay(argv[0]);

rose_retry:
	memset(&conn_cause, 0, sizeof(conn_cause));
	if (default_port == 0)
	{
		if ((fd = connect_to(argv, family, escape, source, &conn_cause)) == -1)
		{
			set_eolmode(User.fd, EOLMODE_TEXT);
			if (fcntl(User.fd, F_SETFL, 0) == -1)
				node_perror("do_connect: fcntl - stdin", errno);
			if (rose_wp_failover && rose_try_failover(conn_cause.cause,
					rose_addr10, rose_tried, &rose_ntried,
					rose_removed, &rose_nremoved, rose_next_call))
			{
				if (*rose_next_call)
					node_msg(T("*** Route unavailable, retrying via %s..."), rose_next_call);
				else
					node_msg(T("*** Route unavailable, retrying via another neighbour..."));
				goto rose_retry;
			}
			rose_restore_failover(rose_removed, rose_nremoved);
			goto done;
		}
	}
	else
	{
		if ((fd = connect_to(argvp, family, escape, source, &conn_cause)) == -1)
		{
			set_eolmode(User.fd, EOLMODE_TEXT);
			if (fcntl(User.fd, F_SETFL, 0) == -1)
				node_perror("do_connect: fcntl - stdin", errno);
			if (rose_wp_failover && rose_try_failover(conn_cause.cause,
					rose_addr10, rose_tried, &rose_ntried,
					rose_removed, &rose_nremoved, rose_next_call))
			{
				if (*rose_next_call)
					node_msg(T("*** Route unavailable, retrying via %s..."), rose_next_call);
				else
					node_msg(T("*** Route unavailable, retrying via another neighbour..."));
				goto rose_retry;
			}
			rose_restore_failover(rose_removed, rose_nremoved);
			goto done;
		}
	}

	/* F6BVP 2026-09-15: a successful connection via an explicitly
	 * dialed ROSE address (numeric DNIC/address, not resolved through
	 * WP -- explicit_rose_call is only set in that case) is a strong
	 * signal that the target really is reachable. If we already have a
	 * White Pages entry for it but it's marked deleted, undelete it --
	 * it was very likely wrongly or prematurely deleted by wpmaint
	 * (see its del_date fix) or another node, and this user just
	 * proved otherwise. Only ever touches a record we already had;
	 * never creates a new one from an arbitrary connection attempt. */
	if (*explicit_rose_call)
	{
		ax25_address call2;
		wp_t wpt2;

		if (ax25_aton_entry(explicit_rose_call, call2.ax25_call) != -1 &&
			wp_open("NODE") == 0)
		{
			if (wp_get(&call2, &wpt2) == 0 && wpt2.is_deleted)
			{
				wpt2.is_deleted = 0;
				wp_set_del_date(&wpt2, 0);
				if (wp_set(&wpt2) == 0)
					node_msg(T("*** White Pages entry for %s restored"), explicit_rose_call);
			}
			wp_close();
		}
	}

	if (connstr)
	{
		usprintf(fd, "%s\n", connstr);
		usflush(fd);
	}

	/* No timeout */
	alarm(0L);

	while (1)
	{
		FD_ZERO(&fdset);
		FD_SET(fd, &fdset);
		FD_SET(User.fd, &fdset);
		if (select(fd + 1, &fdset, 0, 0, 0) == -1)
		{
			node_perror("do_connect: select", errno);
			break;
		}
		if (FD_ISSET(fd, &fdset))
		{
			/* alarm(ConnTimeout); */
			while ((c = usgetc(fd)) != -1)
				usputc(c, User.fd);
			if (errno != EAGAIN)
			{
				if (errno && errno != ENOTCONN)
					node_msg("%s", strerror(errno));
				break;
			}
		}
		if (FD_ISSET(User.fd, &fdset))
		{
			/* alarm(ConnTimeout); */
			cpt = 0;
			while ((c = usgetc(User.fd)) != -1)
			{

		if (escape != -1)
				{
					if (cpt == 0 && c == '~')
						cpt = 1;
					else if (cpt == 1 && c == '.')
						cpt = 2;
					else if (cpt == 2 && c == '\r')
						cpt = 3;
					else if (c == '\r')
						cpt = 0;
					else
						cpt = 4;
				}
				usputc(c, fd);
			}
			if (escape != -1 && cpt == 3)
			{
				readline(User.fd);
				break;
			}
			if (errno != EAGAIN)
			{
				stay = 0;
				break;
			}
		}
		usflush(fd);
		usflush(User.fd);
	}
	end_io(fd);

	origin[0] = '\0';
	if (family == AF_ROSE)
	{
		/* Get the cause and diag */
		memset(&facilities, 0x00, sizeof(struct rose_facilities_struct));
		memset(&rose_cause, 0x00, sizeof(struct rose_cause_struct));

		rose_cause.cause = ROSE_LOCAL_PROCEDURE;
		rose_cause.diagnostic = 0;

		ioctl(fd, SIOCRSGCAUSE, &rose_cause);

		if ((ioctl(fd, SIOCRSGFACILITIES, &facilities) != -1) &&
			(facilities.fail_call.ax25_call[0]))
		{
			sprintf(origin, " at %s @ %s",
					ax25_ntoa(&facilities.fail_call),
					fpac2asc(&facilities.fail_addr));
		}
		else

		if (*origin)
			node_msg(T("*** Disconnected%s"), origin);

		if (rose_wp_failover && rose_try_failover(rose_cause.cause,
				rose_addr10, rose_tried, &rose_ntried,
				rose_removed, &rose_nremoved, rose_next_call))
		{
			close(fd);
			if (*rose_next_call)
				node_msg(T("*** Route unavailable, retrying via %s..."), rose_next_call);
			else
				node_msg(T("*** Route unavailable, retrying via another neighbour..."));
			goto rose_retry;
		}

		node_msg("*** %02X%02X - %s", rose_cause.cause,
				 rose_cause.diagnostic, reason(rose_cause.cause));

	}

	rose_restore_failover(rose_removed, rose_nremoved);
	}

	close(fd);
	fpaclog(LOGLVL_GW, "Disconnected from %s%s", User.dl_name, origin);
	if (stay)
	{
		set_eolmode(User.fd, EOLMODE_TEXT);
		if (fcntl(User.fd, F_SETFL, 0) == -1)
			node_perror("do_connect: fcntl - stdin", errno);
		node_msg(T("*** Reconnected to %s"), NodeId);
	}
	else
		logout("No reconnect");

done:
	if (wp_opened)
		wp_close();
	if (argvp_base)
	{
		free(argvp_base[0]);
		free(argvp_base[1]);
		free(argvp_base[2]);
		free(argvp_base);
	}
	return 0;
}

/*
*
* Help for command finger
*
USAGE
        Finger [<username>][@<hostname>]

DESCRIPTION
        Retrieves information about users of a system. If the user
        name is omitted, shows the users currently logged on the 
        host. If the hostname is omitted, defaults to the local host.

EXAMPLES
        finger @soul.oh7rba.ampr.org
        finger oh7lzb@soul.oh7rba.ampr.org
        finger oh7lzb

* 
*/

int do_finger(int argc, char **argv)
{
	int fd, c;
	char *name, *addr[3], *cp;

	if (argc < 2)
	{
		name = "";
		addr[0] = "localhost";
	}
	else if ((cp = strchr(argv[1], '@')) != NULL)
	{
		*cp = 0;
		name = argv[1];
		addr[0] = ++cp;
	}
	else
	{
		name = argv[1];
		addr[0] = "localhost";
	}
	addr[1] = "finger";
	addr[2] = NULL;
	if ((fd = connect_to(addr, AF_INET, -1, "(finger) ", NULL)) != -1)
	{
		if (fcntl(fd, F_SETFL, 0) == -1)
			node_perror("do_finger: fcntl - fd", errno);
		init_io(fd, 1024, INET_EOL);
		usprintf(fd, "%s\n", name);
		usflush(fd);
		while ((c = usgetc(fd)) != -1)
			usputc(c, User.fd);
		end_io(fd);
		close(fd);
		node_msg(T("*** Reconnected to %s"), NodeId);
	}
	set_eolmode(User.fd, EOLMODE_TEXT);
	if (fcntl(User.fd, F_SETFL, 0) == -1)
		node_perror("do_finger: fcntl - stdin", errno);
	return 0;
}

/*
 * Returns difference of tv1 and tv2 in milliseconds.
 */
static long calc_rtt(struct timeval tv1, struct timeval tv2)
{
	struct timeval tv;

	tv.tv_usec = tv1.tv_usec - tv2.tv_usec;
	tv.tv_sec = tv1.tv_sec - tv2.tv_sec;
	if (tv.tv_usec < 0)
	{
		tv.tv_sec -= 1L;
		tv.tv_usec += 1000000L;
	}
	return ((tv.tv_sec * 1000L) + (tv.tv_usec / 1000L));
}

/*
 * Checksum routine for Internet Protocol family headers (C Version)
 */
static unsigned short in_cksum(unsigned char *addr, int len)
{
	int nleft = len;
	unsigned char *w = addr;
	unsigned int sum = 0;
	unsigned short answer = 0;

	/*
	 * Our algorithm is simple, using a 32 bit accumulator (sum), we add
	 * sequential 16 bit words to it, and at the end, fold back all the
	 * carry bits from the top 16 bits into the lower 16 bits.
	 */
	while (nleft > 1)
	{
		sum += (*(w + 1) << 8) + *(w);
		w += 2;
		nleft -= 2;
	}

	/* mop up an odd byte, if necessary */
	if (nleft == 1)
	{
		sum += *w;
	}

	/* add back carry outs from top 16 bits to low 16 bits */
	sum = (sum >> 16) + (sum & 0xffff);	/* add hi 16 to low 16 */
	sum += (sum >> 16);			/* add carry */
	answer = ~sum;				/* truncate to 16 bits */
	return answer;
}

/*Help for command pin

USAGE
        PIng <hostname> [<length>]

DESCRIPTION
        Checks if a host can be reached trough the network by sending
        an ICMP Echo Request packet to the host and waiting for it to
        reply. If a reply is received the round-trip-time (RTT)
        between the local and remote hosts is shown.

        If an optional length is specified the data portion of the
        packet is filled with length number of bytes.

EXAMPLE
        ping soul.oh7rba.ampr.org
*/
int do_ping(int argc, char **argv)
{
	static int sequence = 0;
	unsigned char buf[256];
	struct hostent *hp;
	struct sockaddr_in to, from;
	struct protoent *prot;
	struct icmphdr *icp;
	struct timeval tv1, tv2;
	struct iphdr *ip;
	fd_set fdset;
	int fd, i, id, len = sizeof(struct icmphdr);
	unsigned int salen = sizeof(struct sockaddr);

	if (argc < 2)
	{
		node_msg("Usage: ping <host> [<size>]");
		return 0;
	}
	if (argc > 2)
	{
		len = atoi(argv[2]) + sizeof(struct icmphdr);
		if (len > 256)
		{
			node_msg("Maximum length is %d", 256 - sizeof(struct icmphdr));
			return 0;
		}
	}
	if ((hp = gethostbyname(argv[1])) == NULL)
	{
		node_msg(T("Unknown host %s"), argv[1]);
		return 0;
	}
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_addr.s_addr = ((struct in_addr *) (hp->h_addr))->s_addr;
	if ((prot = getprotobyname("icmp")) == NULL)
	{
		node_msg("Unknown protocol icmp");
		return 0;
	}
	if ((fd = socket(AF_INET, SOCK_RAW, prot->p_proto)) == -1)
	{
		node_perror("do_ping: socket", errno);
		return 0;
	}
	node_msg("Pinging %s... Type <RETURN> to abort", inet_ntoa(to.sin_addr));
	usflush(User.fd);
	strcpy(User.dl_name, inet_ntoa(to.sin_addr));
	User.dl_type = AF_INET;
	User.state = STATE_PINGING;
	update_user();
	/*
	 * Fill the data portion (if any) with some garbage.
	 */
	for (i = sizeof(struct icmphdr); i < len; i++)
		buf[i] = (i - sizeof(struct icmphdr)) & 0xff;
	/*
	 * Fill in the icmp header.
	 */
	id = getpid() & 0xffff;
	icp = (struct icmphdr *) buf;
	icp->type = ICMP_ECHO;
	icp->code = 0;
	icp->checksum = 0;
	icp->un.echo.id = id;
	icp->un.echo.sequence = sequence++;
	/*
	 * Calculate checksum.
	 */
	icp->checksum = in_cksum(buf, len);
	/*
	 * Take the time and send the packet.
	 */
	gettimeofday(&tv1, NULL);
	if (sendto(fd, buf, len, 0, (struct sockaddr *) &to, salen) != len)
	{
		node_perror("do_ping: sendto", errno);
		close(fd);
		return 0;
	}
	/*
	 * Now wait for it to come back (or user to abort).
	 */
	if (fcntl(User.fd, F_SETFL, O_NONBLOCK) == -1)
	{
		node_perror("do_ping: fcntl - stdin", errno);
		close(fd);
		return 0;
	}
	while (1)
	{
		FD_ZERO(&fdset);
		FD_SET(fd, &fdset);
		FD_SET(User.fd, &fdset);
		if (select(fd + 1, &fdset, 0, 0, 0) == -1)
		{
			node_perror("do_ping: select", errno);
			break;
		}
		if (FD_ISSET(fd, &fdset))
		{
			if ((len =
				 recvfrom(fd, buf, 256, 0, (struct sockaddr *) &from,
						  &salen)) == -1)
			{
				node_perror("do_ping: recvfrom", errno);
				break;
			}
			gettimeofday(&tv2, NULL);
			ip = (struct iphdr *) buf;
			/* Is it long enough? */
			if (len >= (ip->ihl << 2) + sizeof(struct icmphdr))
			{
				len -= ip->ihl << 2;
				icp = (struct icmphdr *) (buf + (ip->ihl << 2));
				/* Is it ours? */
				if (icp->type == ICMP_ECHOREPLY && icp->un.echo.id == id
					&& icp->un.echo.sequence == sequence - 1)
				{
					node_msg("%s rtt: %ldms", inet_ntoa(from.sin_addr),
							 calc_rtt(tv2, tv1));
					break;
				}
			}
		}
		if (FD_ISSET(User.fd, &fdset))
		{
			if (readline(User.fd) != NULL)
			{
				node_msg(T("Aborted"));
				break;
			}
			else if (errno != EAGAIN)
			{
				break;
			}
		}
	}
	if (fcntl(User.fd, F_SETFL, 0) == -1)
		node_perror("do_ping: fcntl - stdin", errno);
	close(fd);
	return 0;
}
