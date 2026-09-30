/* FPAC 
 * node/command.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/utsname.h>
/*#include <sys/types.h>*/
#include <dirent.h>
/*#include <sys/stat.h>*/
#include <sys/ioctl.h>
/*#include <sys/socket.h>*/
/*#include <netinet/in.h>*/
#include <arpa/inet.h>
#include <netdb.h>

#include <asm/param.h>	/* for HZ */

#include "config.h"
#include "node.h"
#include "ax25compat.h"
#include "wp.h"
#include "io.h"
#include "colors.h"
#include "sysinfo.h"

// Lets there be color !
int Colored =1;

struct cmd *Nodecmds = NULL;

#define	ROSE_DEFAULT_MAXVC	50	/* Maximum number of VCs per neighbour in linux/include/net/rose.h */
#define nb_wp() 1

/*add_internal_cmd's args are:
 -list to add the command to (&Nodecmds)
 -command name
 -num of chars of cmd name required (Alias = 1, HOst = 2)
 -is visible? (1 = yes, 0 = no)
 -subroutine to run for command
*/

int wpcheck(void)
{
	FILE *fptr;
	wp_header wph;
	int n = 0;

	if (wp_open("NODE")== 0)
	{
		if ((n = wp_nb_records()) < 0)
			n = 0;
		wp_close();
	}

	if (n == 0)
	{
		fptr = fopen(FPACWP, "r");
		if (fptr != NULL)
		{
			if (fread(&wph, sizeof(wph), 1, fptr) == 1)
				n = wph.nb_record;
			fclose(fptr);
		}
	}
	tprintf("FPAC White Pages : %d\n", n);
/*	free(wp); */

	return (0);
}

/* prototypes anticipés */
int do_hrd(int argc, char **argv);
int do_rs_neigh(int argc, char **argv);
int do_rs_nodes(int argc, char **argv);
int do_rose_ckt(int argc, char **argv);
int do_stat_heure(int argc, char **argv);
int do_stat_jour(int argc, char **argv);

void init_nodecmds(void)
{
	add_internal_cmd(&Nodecmds, "?", 1, 1, do_help);
	add_internal_cmd(&Nodecmds, "Alias", 1, 1, do_alias);
	add_internal_cmd(&Nodecmds, "APplications", 2, 1, do_application);
	add_internal_cmd(&Nodecmds, "Bye", 1, 1, do_bye);
	add_internal_cmd(&Nodecmds, "COLor", 3, 1, do_color);
	add_internal_cmd(&Nodecmds, "Connect", 1, 1, do_connect);
	add_internal_cmd(&Nodecmds, "Dest", 1, 1, do_dest);
	add_internal_cmd(&Nodecmds, "Finger", 1, 1, do_finger);
	add_internal_cmd(&Nodecmds, "Help", 1, 1, do_help);
	add_internal_cmd(&Nodecmds, "HOst", 2, 1, do_host);
	add_internal_cmd(&Nodecmds, "Info", 1, 1, do_help);
add_internal_cmd(&Nodecmds, "LAng", 2, 1, do_lang);
	add_internal_cmd(&Nodecmds, "Links", 1, 1, do_links);
	add_internal_cmd(&Nodecmds, "HRD", 3, 1, do_hrd);
	add_internal_cmd(&Nodecmds, "Mheard", 1, 1, do_mheard);
	add_internal_cmd(&Nodecmds, "NEtrom", 2, 1, do_netrom);
	add_internal_cmd(&Nodecmds, "ROse",    2, 1, do_rose_ckt);
	add_internal_cmd(&Nodecmds, "RS_NEigh", 4, 1, do_rs_neigh);
	add_internal_cmd(&Nodecmds, "RS_NOdes", 4, 1, do_rs_nodes);
	add_internal_cmd(&Nodecmds, "Nodes", 1, 1, do_rose);
	add_internal_cmd(&Nodecmds, "PIng", 2, 1, do_ping);
	add_internal_cmd(&Nodecmds, "Ports", 1, 1, do_ports);
	add_internal_cmd(&Nodecmds, "Quit", 1, 1, do_bye);
	add_internal_cmd(&Nodecmds, "Routes", 1, 1, do_routes);
	add_internal_cmd(&Nodecmds, "Status", 1, 1, do_status);
	add_internal_cmd(&Nodecmds, "STHeure", 3, 1, do_stat_heure);
	add_internal_cmd(&Nodecmds, "STJour",  3, 1, do_stat_jour);
	add_internal_cmd(&Nodecmds, "SYSop", 3, 1, do_sysop);
	add_internal_cmd(&Nodecmds, "Telnet", 1, 1, do_connect);
	add_internal_cmd(&Nodecmds, "Users", 1, 1, do_users);
	add_internal_cmd(&Nodecmds, "Wp", 1, 1, do_wp);
	add_internal_cmd(&Nodecmds, "MRoutes", 2, 1, do_manage_routes);

	/*
	   add_internal_cmd(&Nodecmds, "Connect",  1, 1, do_connect);
	   add_internal_cmd (&Nodecmds, "TAlk", 2, 1, do_talk);
	   add_internal_cmd(&Nodecmds, "Telnet",   1, 1, do_connect);
	   add_internal_cmd(&Nodecmds, "Users",    1, 1, user_list);
	 */
}

int callmatch(char *call, char *ref)
{
	while (isalnum(*call) && isalnum(*ref))
	{
		if (toupper(*call) != toupper(*ref))
			break;
		++call;
		++ref;
	}

	if (isalnum(*call) || isalnum(*ref))
		return 0;
	return 1;
}

/*
 * read_proc_rs_nodes() of libax25 builds its list by prepending each
 * line, i.e. in the REVERSE order of /proc/net/rose_nodes. The kernel
 * keeps rose_node_list sorted by decreasing mask and rose_get_neigh()
 * takes the first matching entry, so the order matters: this returns
 * the routes in kernel order (most specific first).
 */
struct proc_rs_nodes *read_proc_rs_nodes_ordered(void)
{
	struct proc_rs_nodes *list, *next, *prev = NULL;

	for (list = read_proc_rs_nodes(); list != NULL; list = next)
	{
		next = list->next;
		list->next = prev;
		prev = list;
	}
	return prev;
}

/*
 * Connect callsign (L2call, the one a user types to reach the node)
 * of the FPAC node owning a 10 digit ROSE address, looked up in the
 * White Pages node list. The routing tables only know the L3 callsigns
 * of the neighbours. Returns NULL when the node is not in the WP.
 */
static char *wp_connect_call(wp_t *wpn, int nwp, const char *addr10)
{
	static char call[10];
	int i;

	for (i = 0; i < nwp; i++)
	{
		if (wpn[i].is_deleted)
			continue;
		if (strncmp(rose_ntoa(&wpn[i].address.srose_addr), addr10, 10) == 0)
		{
			strcpy(call, ax25_ntoa(&wpn[i].address.srose_call));
			return call;
		}
	}
	return NULL;
}

char *roseaddr(char *addr)
{
	static char buf[12];

	if (*addr == '*')
		strcpy(buf, "None       ");
	else
	{
		memcpy(buf, addr, 4);
		buf[4] = ',';
		memcpy(buf + 5, addr + 4, 6);
		buf[11] = '\0';
	}
	return (buf);
}

void logout(char *reason)
{
	end_io(User.fd);
	close(User.fd);
	logout_user();
	/* ipc_close(); */
	fpaclog(LOGLVL_LOGIN, "%s @ %s logged out: %s", User.call, User.ul_name,
		reason);
	free_cmdlist(Nodecmds);
	Nodecmds = NULL;
	exit(0);
}

int do_bye(int argc, char **argv)
{
	(void)argc; (void)argv;
	logout("Bye");
	return 0;					/* Keep gcc happy */
}

/* Toggle colored menu mode ON or OFF*/
int do_color(int argc, char **argv)
{
	(void)argc; (void)argv;
	Colored = !Colored;
	return 0;					/* Keep gcc happy */
}

static int callcmp(char *ref, char *call)
{
	char *s;
	char str[40];

	if (strchr(ref, '-') == NULL)
	{
		/* No SSID in reference. Match any SSID */
		strcpy(str, call);
		if ((s = strchr(str, '-')) != NULL)
			*s = '\0';
		call = str;
	}
	return (strcasecmp(ref, call));
}


/*
 * NetRom links run over an AX.25 device (ax0), with the callsign of the
 * NetRom port as local callsign. Return that NetRom port, or NULL.
 */
static char *nr_port_by_call(const char *call)
{
	char *port = NULL, *addr;

	while ((port = nr_config_get_next(port)) != NULL)
	{
		addr = nr_config_get_addr(port);
		if (addr != NULL && callsign_eq(addr, call))
			return port;
	}
	return NULL;
}

int netrom_node_is_connected(char *call)
{
	struct proc_nr_neigh *p, *list;
	int state = 0;

	if ((list = read_proc_nr_neigh()) == NULL)
	{
/*		if (errno)
			perror("netrom_node_is_connected: read_proc_nr_neigh");*/
		return (state);
	}
	
	for (p = list; p != NULL; p = p->next)
	{
		if (!strncmp (p->call, call, 10)) {
				state = 1;
				break; 
		}
	}
	free_proc_nr_neigh(list);
	return (state);
}

int node_is_connected(char *call)
{
	struct proc_rs_neigh *p, *list;
	int state = 0;

	if ((list = read_proc_rs_neigh()) == NULL)
	{
/*		if (errno)
			perror("node_is_connected: read_proc_rs_neigh");*/
		return (state);
	}
	
	for (p = list; p != NULL; p = p->next)
	{
		if (!strncmp (p->call, call, 10)) {
			if (!strcmp("yes", p->restart)) {
				state = 1;
				break;
			}
		}
	}
	free_proc_rs_neigh(list);
	return (state);
}

/* ------------------------------------------------------------------ */
/* do_hrd : equivalent interne de "mheard -d m" avec couleurs         */
/* ------------------------------------------------------------------ */
static char *hrd_types[] = {
	"SABM", "SABME", "DISC", "UA", "DM",
	"RR", "RNR", "REJ", "FRMR", "I", "UI", "????"
};
#define HRD_NTYPES ((int)(sizeof(hrd_types)/sizeof(hrd_types[0])))

int do_hrd(int argc, char **argv)
{
	FILE *fp;
	struct mheard_struct mh;
	char *call, *u;

	if ((argc > 1) && (*argv[1] == '?'))
	{
		node_msg("usage : hrd");
		return 0;
	}

	if ((fp = fopen(DATA_MHEARD_FILE, "r")) == NULL)
	{
		node_perror(DATA_MHEARD_FILE, errno);
		return 0;
	}

	if (Colored)
		tprintf("%sCallsign   Port    Packets  Type  PIDs%s\n",
				NodeColors.en_tete, ResetColor);
	else
		tprintf("Callsign   Port    Packets  Type  PIDs\n");

	while (fread(&mh, sizeof(struct mheard_struct), 1, fp) == 1)
	{
		if (!mh.count)
			continue;
		if (mh.type >= (unsigned int)HRD_NTYPES)
			continue;
		call = ax25_ntoa(&mh.from_call);
		u = strstr(call, "-0");
		if (u != NULL)
			*u = '\0';

		if (Colored)
			tprintf("%s%-9s%s  %-5s  %8u %5s",
					NodeColors.indicatif, call, ResetColor,
					mh.portname, mh.count, hrd_types[mh.type]);
		else
			tprintf("%-9s  %-5s  %8u %5s",
					call, mh.portname, mh.count, hrd_types[mh.type]);

		if (mh.mode & MHEARD_MODE_ARP)     tprintf(" ARP");
		if (mh.mode & MHEARD_MODE_FLEXNET) tprintf(" FlexNet");
		if (mh.mode & MHEARD_MODE_IP_DG)   tprintf(" IP-DG");
		if (mh.mode & MHEARD_MODE_IP_VC)   tprintf(" IP-VC");
		if (mh.mode & MHEARD_MODE_NETROM)  tprintf(" NET/ROM");
		if (mh.mode & MHEARD_MODE_ROSE)    tprintf(" Rose");
		if (mh.mode & MHEARD_MODE_SEGMENT) tprintf(" Segment");
		if (mh.mode & MHEARD_MODE_TEXNET)  tprintf(" TexNet");
		if (mh.mode & MHEARD_MODE_TEXT)    tprintf(" Text");
		if (mh.mode & MHEARD_MODE_PSATFT)  tprintf(" PacsatFT");
		if (mh.mode & MHEARD_MODE_PSATPB)  tprintf(" PacsatPB");
		if (mh.mode & MHEARD_MODE_UNKNOWN) tprintf(" Unknown");
		tprintf("\n");
	}
	fclose(fp);
	return 0;
}

/* ------------------------------------------------------------------ */
/* do_rose_ckt : equivalent colorisé de "cat /proc/net/rose"          */
/* ------------------------------------------------------------------ */
int do_rose_ckt(int argc, char **argv)
{
	FILE *fp;
	char line[256];
	int first = 1;
	char dest_addr[16], dest_call[16], src_addr[16], src_call[16];
	char dev[16], lci[8], neigh_s[8], idle[16];
	int st, vs, vr, va, t, t1, t2, t3, hb, sndq, rcvq, inode;

	if ((argc > 1) && (*argv[1] == '?'))
	{
		node_msg("usage : ro");
		return 0;
	}

	if ((fp = fopen("/proc/net/rose", "r")) == NULL)
	{
		node_perror("/proc/net/rose", errno);
		return 0;
	}

	while (fgets(line, sizeof(line), fp))
	{
		line[strcspn(line, "\n")] = '\0';

		if (first)
		{
			/* ligne d'en-tete */
			if (Colored)
				tprintf("%s%s%s\n", NodeColors.en_tete, line, ResetColor);
			else
				tprintf("%s\n", line);
			first = 0;
			continue;
		}

		/* lignes de données */
		if (sscanf(line,
				"%15s %15s %15s %15s %15s %7s %7s"
				" %d %d %d %d %d %d %d %d %d %15s %d %d %d",
				dest_addr, dest_call, src_addr, src_call,
				dev, lci, neigh_s,
				&st, &vs, &vr, &va,
				&t, &t1, &t2, &t3, &hb,
				idle, &sndq, &rcvq, &inode) >= 10)
		{
			if (Colored)
			{
				/* dest_call en vert si pas "*", src_call toujours en vert */
				if (strcmp(dest_call, "*") != 0)
					tprintf("%-10s %s%-9s%s %-10s %s%-9s%s"
							" %-5s %3s %5s %2d %2d %2d %2d"
							" %3d %3d %3d %3d %3d %8s %5d %5d %d\n",
							dest_addr,
							NodeColors.indicatif, dest_call, ResetColor,
							src_addr,
							NodeColors.indicatif, src_call, ResetColor,
							dev, lci, neigh_s, st, vs, vr, va,
							t, t1, t2, t3, hb, idle, sndq, rcvq, inode);
				else
					tprintf("%-10s %-9s %-10s %s%-9s%s"
							" %-5s %3s %5s %2d %2d %2d %2d"
							" %3d %3d %3d %3d %3d %8s %5d %5d %d\n",
							dest_addr, dest_call,
							src_addr,
							NodeColors.indicatif, src_call, ResetColor,
							dev, lci, neigh_s, st, vs, vr, va,
							t, t1, t2, t3, hb, idle, sndq, rcvq, inode);
			}
			else
				tprintf("%s\n", line);
		}
		else
			tprintf("%s\n", line);
	}
	fclose(fp);
	return 0;
}

/* ------------------------------------------------------------------ */
/* do_rs_neigh : equivalent interne de "cat /proc/net/rose_neigh"     */
/* ------------------------------------------------------------------ */
int do_rs_neigh(int argc, char **argv)
{
	FILE *fp;
	char line[256];
	int first = 1;
	char addr[16], callsign[16], dev[16], mode[8], restart[8];
	int count, use, t0, tf;

	if ((argc > 1) && (*argv[1] == '?'))
	{
		node_msg("usage : rs_neigh");
		return 0;
	}

	if ((fp = fopen("/proc/net/rose_neigh", "r")) == NULL)
	{
		node_perror("/proc/net/rose_neigh", errno);
		return 0;
	}

	while (fgets(line, sizeof(line), fp))
	{
		line[strcspn(line, "\n")] = '\0';

		if (first)
		{
			/* ligne d'en-tete */
			if (Colored)
				tprintf("%s%s%s\n", NodeColors.en_tete, line, ResetColor);
			else
				tprintf("%s\n", line);
			first = 0;
			continue;
		}

		/* lignes de donnees */
		if (sscanf(line, "%15s %15s %15s %d %d %7s %7s %d %d",
				   addr, callsign, dev, &count, &use,
				   mode, restart, &t0, &tf) >= 7)
		{
			if (Colored)
			{
				const char *rst_color = (strcmp(restart, "yes") == 0)
					? NodeColors.ind_oui
					: NodeColors.ind_non;
				tprintf("%s %s%-9s%s %-4s %5d %3d  %-3s %s%s%s %3d %3d\n",
						addr,
						NodeColors.indicatif, callsign, ResetColor,
						dev, count, use, mode,
						rst_color, (strcmp(restart, "yes") == 0) ? " yes" : "  no", ResetColor, t0, tf);
			}
			else
				tprintf("%s\n", line);
		}
		else
			tprintf("%s\n", line);
	}
	fclose(fp);
	return 0;
}

/* ------------------------------------------------------------------ */
/* do_rs_nodes : equivalent interne de "cat /proc/net/rose_nodes"     */
/* ------------------------------------------------------------------ */
int do_rs_nodes(int argc, char **argv)
{
	FILE *fp;
	char line[256];
	int first = 1;
	char address[16], mask[8];
	int n;
	unsigned int neigh1, neigh2, neigh3;

	if ((argc > 1) && (*argv[1] == '?'))
	{
		node_msg("usage : rs_nodes");
		return 0;
	}

	if ((fp = fopen("/proc/net/rose_nodes", "r")) == NULL)
	{
		node_perror("/proc/net/rose_nodes", errno);
		return 0;
	}

	while (fgets(line, sizeof(line), fp))
	{
		line[strcspn(line, "\n")] = '\0';

		if (first)
		{
			/* ligne d'en-tete */
			if (Colored)
				tprintf("%s%s%s\n", NodeColors.en_tete, line, ResetColor);
			else
				tprintf("%s\n", line);
			first = 0;
			continue;
		}

		/* lignes de donnees : address mask n neigh [neigh [neigh]] */
		neigh1 = neigh2 = neigh3 = 0;
		int parsed = sscanf(line, "%15s %7s %d %u %u %u",
							address, mask, &n,
							&neigh1, &neigh2, &neigh3);
		if (parsed >= 3)
		{
			if (Colored)
			{
				tprintf("%s%s%s %s %d",
						NodeColors.adresse, address, ResetColor,
						mask, n);
				if (neigh1) tprintf(" %05u", neigh1);
				if (neigh2) tprintf(" %05u", neigh2);
				if (neigh3) tprintf(" %05u", neigh3);
				tprintf("\n");
			}
			else
				tprintf("%s\n", line);
		}
		else
			tprintf("%s\n", line);
	}
	fclose(fp);
	return 0;
}

/* ------------------------------------------------------------------ */
/* do_stat_file : affiche colorisé un fichier de statistiques FPAC    */
/* (partagé par do_stat_heure et do_stat_jour)                        */
/* ------------------------------------------------------------------ */
static int do_stat_file(const char *filename)
{
	FILE *fp;
	char line[256];
	int first = 1;
	char call[16];
	long data, qual, iframe, rr, rnr, rej, sabm, disc, ua, dm;
	const char *qual_color;

	if ((fp = fopen(filename, "r")) == NULL)
	{
		node_perror((char *)filename, errno);
		return 0;
	}

	while (fgets(line, sizeof(line), fp))
	{
		line[strcspn(line, "\n")] = '\0';

		if (first)
		{
			/* Ligne de titre */
			if (Colored)
				tprintf("%s%s%s\n", NodeColors.en_tete, line, ResetColor);
			else
				tprintf("%s\n", line);
			first = 0;
			continue;
		}

		/* Ligne d'en-tête des colonnes */
		if (strncmp(line, "   Adjacent", 11) == 0)
		{
			if (Colored)
				tprintf("%s%s%s\n", NodeColors.en_tete, line, ResetColor);
			else
				tprintf("%s\n", line);
			continue;
		}

		/* Ligne "to CALL data qual% iframe rr rnr rej sabm disc ua dm" */
		if (strncmp(line, "to ", 3) == 0)
		{
			int n = sscanf(line, "to %15s %ld %ld%% %ld %ld %ld %ld %ld %ld %ld %ld",
						   call, &data, &qual, &iframe,
						   &rr, &rnr, &rej, &sabm, &disc, &ua, &dm);
			if (n == 11)
			{
				if (Colored)
				{
					qual_color = (qual == 0) ? NodeColors.qualite_nulle
					           : (qual <= 50) ? NodeColors.qualite_moyenne
					           : NodeColors.qualite_bonne;
					tprintf("to %s%-10s%s %8ld %s%3ld%%%s %7ld %6ld %4ld %4ld %4ld %4ld %4ld %4ld\n",
							NodeColors.indicatif, call, ResetColor,
							data,
							qual_color, qual, ResetColor,
							iframe, rr, rnr, rej, sabm, disc, ua, dm);
				}
				else
					tprintf("%s\n", line);
			}
			else
				tprintf("%s\n", line);
			continue;
		}

		/* Ligne "fm CALL data iframe rr rnr rej sabm disc ua dm" */
		if (strncmp(line, "fm ", 3) == 0)
		{
			int n = sscanf(line, "fm %15s %ld %ld %ld %ld %ld %ld %ld %ld %ld",
						   call, &data, &iframe,
						   &rr, &rnr, &rej, &sabm, &disc, &ua, &dm);
			if (n == 10)
			{
				if (Colored)
					tprintf("fm %s%-10s%s %8ld      %7ld %6ld %4ld %4ld %4ld %4ld %4ld %4ld\n",
							NodeColors.indicatif, call, ResetColor,
							data, iframe, rr, rnr, rej, sabm, disc, ua, dm);
				else
					tprintf("%s\n", line);
			}
			else
				tprintf("%s\n", line);
			continue;
		}

		/* Lignes vides et autres */
		tprintf("%s\n", line);
	}

	fclose(fp);
	return 0;
}

/* ------------------------------------------------------------------ */
/* do_stat_heure : équivalent colorisé de "cat fpacstat.dat"          */
/* ------------------------------------------------------------------ */
int do_stat_heure(int argc, char **argv)
{
	if ((argc > 1) && (*argv[1] == '?'))
	{
		node_msg("usage : sth  (statistiques de l'heure en cours)");
		return 0;
	}
	return do_stat_file(STATDAT);
}

/* ------------------------------------------------------------------ */
/* do_stat_jour : équivalent colorisé de "cat fpacstat.day"           */
/* ------------------------------------------------------------------ */
int do_stat_jour(int argc, char **argv)
{
	if ((argc > 1) && (*argv[1] == '?'))
	{
		node_msg("usage : stj  (statistiques des dernieres 24h)");
		return 0;
	}
	return do_stat_file(STATDAY);
}

/* ------------------------------------------------------------------ */

struct mheard_list
{
	struct mheard_struct data;
	struct mheard_list *next;
};

static char *time_ago(time_t ago, char *buf)
{
	static char buffer[80];
	char temp[40];
	time_t hour;
	time_t min;
	time_t sec;

	hour = ago / 3600L;
	min = (ago % 3600L) / 60L;
	sec = (ago % 60L);

	if (buf == NULL)
		buf = buffer;

	buf[0] = '\0';

	if (hour)
	{
		sprintf(temp, "%ldh ", hour);
		strcat(buf, temp);
	}

	if (min || hour)
	{
		sprintf(temp, "%02ldm ", min);
		strcat(buf, temp);
	}

	sprintf(temp, "%02lds", sec);
	strcat(buf, temp);

	return (buf);
}

#define NB_HEARD 20
int do_mheard(int argc, char **argv)
{
	int nb;
	FILE *fp;
	struct mheard_struct mh;
	struct mheard_list *list, *new, *tmp, *p;
	char *s, *t, *u;
	char *port = NULL;
	char *call = NULL;
	long ti;
	time_t temps;

	if ((argc > 1) && (*argv[1] == '?'))
	{
		node_msg("usage : mheard [port] [callsign]");
		return (0);
	}

	for (nb = 1; nb < argc; nb++)
	{
		if (wp_check_call(argv[nb]) == 0)
		{
			call = argv[nb];
		}
		else
		{
			port = argv[nb];
			if (ax25_config_get_dev(port) == NULL)
			{
				node_msg("Trying heard callsign on invalid port");
				return 0;
			}
		}
	}

	if ((fp = fopen(DATA_MHEARD_FILE, "r")) == NULL)
	{
		node_perror(DATA_MHEARD_FILE, errno);
		return 0;
	}

	list = NULL;
	nb = 0;

	while (fread(&mh, sizeof(struct mheard_struct), 1, fp) == 1)
	{
		t = ax25_ntoa(&mh.from_call);
		if (wp_check_call(t))
			continue;

		if (call)
		{
			if (callcmp(call, t) != 0)
				continue;
		}

		if (port && (strcasecmp(port, mh.portname)))
			continue;

		if ((new = calloc(1, sizeof(struct mheard_list))) == NULL)
		{
			node_perror("do_mheard: calloc", errno);
			break;
		}
		new->data = mh;
		if (list == NULL || mh.last_heard > list->data.last_heard)
		{
			tmp = list;
			list = new;
			new->next = tmp;
		}
		else
		{
			for (p = list; p->next != NULL; p = p->next)
				if (mh.last_heard > p->next->data.last_heard)
					break;
			tmp = p->next;
			p->next = new;
			new->next = tmp;
		}
		++nb;
	}
	fclose(fp);

	if (list == NULL)
	{
		node_msg("Nothing heard");
		return (0);
	}

	if (nb > NB_HEARD)
		nb = NB_HEARD;

	if (call)
	{
		if (port)
			tprintf("Last %d Heard details for %s on port %s :\n", nb, call,
					port);
		else
			tprintf("Last %d Heard details for %s on all ports :\n", nb,
					call);
		if (Colored)
			tprintf("%sCallsign  Port    Pkts-rcvd I-Frames S-Frames U-Frames Time ago%s\n",
					NodeColors.en_tete, ResetColor);
		else
			tprintf("Callsign  Port    Pkts-rcvd I-Frames S-Frames U-Frames Time ago\n");
	}
	else if (port)
	{
		tprintf("Last %d Heard list for port %s :\n", nb, port);
		if (Colored)
			tprintf("%sCallsign  Port    Pkts-rcvd Mode Time ago%s\n", NodeColors.en_tete, ResetColor);
		else
			tprintf("Callsign  Port    Pkts-rcvd Mode Time ago\n");
	}
	else
	{
		tprintf("Last %d Heard list for all ports :\n", nb);
		if (Colored)
			tprintf("%sCallsign  Port    Pkts-rcvd Mode Time ago%s\n", NodeColors.en_tete, ResetColor);
		else
			tprintf("Callsign  Port    Pkts-rcvd Mode Time ago\n");
	}

	nb = 0;

	temps = time(NULL);

	while (list != NULL)
	{
		if (nb++ < NB_HEARD)
		{
			t = ax25_ntoa(&list->data.from_call);
			ti = temps - list->data.last_heard;

			if (call)
			{
				if(Colored)
				tprintf("%s%-9s%s %-7.7s %-9ld %-8ld %-8ld %-8ld %s\n",
						NodeColors.indicatif, t, ResetColor,
						list->data.portname, list->data.count,
						list->data.sframes, list->data.iframes,
						list->data.uframes, time_ago(ti, NULL));
				else
				tprintf("%-9s %-7.7s %-9ld %-8ld %-8ld %-8ld %s\n",
						t,
						list->data.portname, list->data.count,
						list->data.sframes, list->data.iframes,
						list->data.uframes, time_ago(ti, NULL));
						
			}
			else
			{
				if ((u = strstr(t, "-0")) != NULL)
					*u = '\0';

				if (list->data.mode & MHEARD_MODE_ROSE)
					s = "FPAC";
				else if (list->data.mode & MHEARD_MODE_NETROM)
					s = "NRom";
				else if (list->data.mode & MHEARD_MODE_FLEXNET)
					s = "Flex";
				else if (list->data.mode & MHEARD_MODE_TEXNET)
					s = "TexN";
				else if (list->data.mode & MHEARD_MODE_TEXT)
					s = "AX25";
				else
					s = "None";
				if(Colored)
				tprintf("%s%-9s%s %-7.7s %-9ld %-4s %s\n",
						NodeColors.indicatif, t, ResetColor,
						list->data.portname, list->data.count, s,
						time_ago(ti, NULL));
				else
				tprintf("%-9s %-7.7s %-9ld %-4s %s\n",
						t,
						list->data.portname, list->data.count, s,
						time_ago(ti, NULL));

			}
		}
		tmp = list;
		list = list->next;
		free(tmp);
	}
	return 0;
}

int do_help(int argc, char **argv)
{
	FILE *fp;
	char fname[256], line[256];
	struct cmd *cmdp;
	int i = 0;
	int found, len;
	DIR *dir;
	struct dirent *ent;
	char *ptr;


	if (*argv[0] == '?')
	{							/* "?"      */
		if (is_sysop())
			node_msg("Sysop:");
		for (cmdp = Nodecmds; cmdp != NULL; cmdp = cmdp->next)
		{
			if (!cmdp->valid)
				continue;

			tprintf("%s%s", i ? ", " : "", cmdp->name);
			if (++i == 10)
			{
				tprintf("\n");
				i = 0;
			}
		}
		if (is_sysop())
		{
			for (cmdp = Syscmds; cmdp != NULL; cmdp = cmdp->next)
			{
				if (!cmdp->valid)
					continue;

				tprintf("%s%s", i ? ", " : "", cmdp->name);
				if (++i == 10)
				{
					tprintf("\n");
					i = 0;
				}
			}
		}
		if (i)
			tprintf("\n");
		return 0;
	}

	if (*argv[0] == 'i')
	{							/* "info"   */
		strcpy(fname, LINUX_VERSION);

		if ((fp = fopen(fname, "r")) == NULL)
			return 0;	
		while (fgets(line, 256, fp) != NULL)
			tputs(line);
		tputs("\n");
		fclose(fp);

	/* "info"   */
		strcpy(fname, FPAC_INFO_FILE);
		node_msg("%s v %s (built %s) for LINUX\n", "FPAC-Node", VERSION,
				 __DATE__);

	}
	else if (argc == 1)
	{							/* "help"   */
		snprintf(fname, sizeof(fname), "%s%s", FPAC_HELP_DIR, "Help.hlp");
	}
	else
	{							/* "help <cmd>" */
		found = 0;
		dir = opendir(FPAC_HELP_DIR);
		if (dir)
		{
			/* Search in the help directory the best matching name */
			for (;;)
			{
				ent = readdir(dir);
				if (ent == NULL)
					break;

				ptr = ent->d_name;

				/* Counts the minimal number of letters */
				len = 0;
				while (isupper(*ptr))
				{
					++len;
					++ptr;
				}

				if ((len == 0) || (len < (int)strlen(argv[1])))
					len = strlen(argv[1]);

				if (strncasecmp(ent->d_name, argv[1], len) == 0)
				{
					snprintf(fname, sizeof(fname), "%s%s",
							 FPAC_HELP_DIR, ent->d_name);
					found = 1;
					break;
				}
			}

			if (!found)
			{
				snprintf(fname, sizeof(fname), "%s%s",
						 FPAC_HELP_DIR, "not_found.hlp");
			}

			closedir(dir);
		}
	}
	if ((fp = fopen(fname, "r")) == NULL)
	{
		if (*argv[0] != 'i')
			node_msg("No help for command %s", argv[1] ? argv[1] : "help");
		return 0;
	}
	if (*argv[0] != 'i')
		node_msg("Help for command %s", argv[1] ? argv[1] : "help");
	while (fgets(line, 256, fp) != NULL)
		tputs(line);
	tputs("\n");
	fclose(fp);
	return 0;
}

 
int do_application(int argc, char **argv)
{
	appli_t *al;
	(void)argc; (void)argv;

	tputs("Applications: \n");

	if (cfg.appli == NULL)
	{
		tprintf("No application found\n");
	}
	else
	{
		for (al = cfg.appli; al; al = al->next)
			tprintf(" %-10s : %s\n", al->call, al->appli);
	}

	tputs("----\n");
	return 0;
}

int do_alias(int argc, char **argv)
{
	alias_t *al;
	(void)argc; (void)argv;

	tputs("Aliases: \n");

	if (cfg.alias == NULL)
	{
		tprintf("No alias found\n");
	}
	else
	{
		for (al = cfg.alias; al; al = al->next)
			tprintf(" %-10s : %s\n", al->alias, al->path);
	}

	tputs("----\n");
	return 0;
}

int do_host(int argc, char **argv)
{
	struct hostent *h;
	struct in_addr addr;
	char **p, *cp;

	if (argc < 2)
	{
		node_msg("Usage: host <hostname>|<ip address>");
		return 0;
	}
	if (inet_aton(argv[1], &addr) != 0)
		h = gethostbyaddr((char *) &addr, sizeof(addr), AF_INET);
	else
		h = gethostbyname(argv[1]);
	if (h == NULL)
	{
		switch (h_errno)
		{
		case HOST_NOT_FOUND:
			cp = "Unknown host";
			break;
		case TRY_AGAIN:
			cp = "Temporary name server error";
			break;
		case NO_RECOVERY:
			cp = "Non-recoverable name server error";
			break;
		case NO_ADDRESS:
			cp = "No address";
			break;
		default:
			cp = "Unknown error";
			break;
		}
		node_msg("%s", cp);
		return 0;
	}
	node_msg("Host name information for %s:", argv[1]);
	tprintf("Hostname   : %s\n", h->h_name);
	tputs("Aliases    :");
	p = h->h_aliases;
	while (*p != NULL)
	{
		tprintf(" %s", *p);
		p++;
	}
	tputs("\nAddress(es):");
	p = h->h_addr_list;
	while (*p != NULL)
	{
		addr.s_addr = ((struct in_addr *) (*p))->s_addr;
		tprintf(" %s", inet_ntoa(addr));
		p++;
	}
	tputs("\n");
	return 0;
}

/* Port descriptions read from axports/rsports/nrports may start with blanks */
static char *skip_blanks(char *s)
{
	while (s != NULL && isspace((unsigned char)*s))
		s++;
	return s;
}

int do_ports(int argc, char **argv)
{
	char *cp = NULL;

	if (rs_config_get_next(cp) == NULL)
		(void)rs_config_load_ports();

	if ((argc > 1) && (*argv[1] == '?'))
	{
		node_msg("usage : ports");
		return (0);
	}

	/* Callsign column: AX.25 and NetRom port callsign, ROSE port address */
	/* Fixed widths with spaces (not tabs) so that headers and rows line up */
	if (Colored)
		node_msg("Ports:\n%s%-7s%s %s%-10s%s %s%-6s%s %sDescription%s", NodeColors.en_tete, "Port", ResetColor, NodeColors.en_tete, "Callsign", ResetColor, NodeColors.en_tete, "Dev", ResetColor, NodeColors.en_tete, ResetColor);
	else
		node_msg("Ports:\n%-7s %-10s %-6s Description", "Port", "Callsign", "Dev");

	while ((cp = ax25_config_get_next(cp)) != NULL)
	{
		tprintf("%-7s %-10s %-6s %s\n", cp, ax25_config_get_addr(cp), ax25_config_get_dev(cp), skip_blanks(ax25_config_get_desc(cp)));
	}
	while ((cp = rs_config_get_next(cp)) != NULL)
	{
		tprintf("%-7s %-10s %-6s %s\n", cp, rs_config_get_addr(cp), rs_config_get_dev(cp), skip_blanks(rs_config_get_desc(cp)));
	}
	while ((cp = nr_config_get_next(cp)) != NULL)
	{
		tprintf("%-7s %-10s %-6s %s\n", cp, nr_config_get_addr(cp), nr_config_get_dev(cp), skip_blanks(nr_config_get_desc(cp)));
	}


	return 0;
}

int do_users(int argc, char **argv)
{
	struct proc_ax25 *p, *list;
	struct proc_rs *rp, *rlist;
	struct proc_rs_route *tp, *tlist;
	struct proc_rs_neigh *pv, *listv;
	char *cp;
	int first;
	int i,j,len;
/*	char neigh[20],	char nei1[20], nei2[20];*/

	if ((argc > 1) && (*argv[1] == '?'))
	{
		node_msg("usage : users [callsign]");
		return (0);
	}

	if ((p = calloc(1, sizeof(struct proc_ax25))) == NULL)
		return (0);
	if ((rp = calloc(1, sizeof(struct proc_rs))) == NULL)
		return (0);
	if ((tp = calloc(1, sizeof(struct proc_rs_route))) == NULL)
		return (0);
	if ((listv = calloc(1, sizeof(struct proc_rs_neigh))) == NULL)
		return (0);

	first = 1;

	if ((list = read_proc_ax25()) == NULL)
	{
		if (errno)
			node_perror("do_users: read_proc_ax25 ", errno);
		return 0;
	}
	
	for (p = list; p != NULL; p = p->next)
	{
		if (argc > 1 && strcasecmp(argv[1], "*")
			&& callcmp(argv[1], p->dest_addr)
			&& callcmp(argv[1], p->src_addr))
			continue;

		if ((argc < 2) && !strcmp(p->dest_addr, "*"))
			continue;

		if ((wp_check_call(p->src_addr) != 0)
			&& (wp_check_call(p->dest_addr) != 0))
			continue;

		if (first)
		{
			first = 0;
			node_msg("Users - AX.25 Level 2 sessions :");
			if (Colored)	
				tprintf("%sPort%s   %sCallsign%s     %sCallsign%s  %sDigi 1%s   %sDigi 2%s   %sAX.25 state%s  %sROSE state%s  %sNetRom status%s", 
				NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor);
			else
				tprintf("Port   Callsign     Callsign  Digi 1   Digi 2   AX.25 state  ROSE state  NetRom status");
			if (is_sysop())
			{
				if (Colored)
					tprintf(" %sUnack%s   %sT1%s      %sT3%s      %sRetr%s  %sRtt%s %sSnd-Q Rcv-Q%s",
					NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor);
				else
					tprintf(" Unack   T1      T3      Retr  Rtt Snd-Q Rcv-Q");
			}
			tprintf("\n");
		}

		len = strlen(p->src_addr);
		if ((len > 0) && (p->src_addr[len - 1] == '*'))
			p->src_addr[len - 1] = '\0';

		cp = ax25_config_get_name(p->dev);
		if (cp != NULL)
		{
			/*
			 * NetRom and ROSE links run over the AX.25 device too: show
			 * the NetRom port, or the ROSE port for the L3call of the
			 * node, instead of the AX.25 device port.
			 */
			char *lp = nr_port_by_call(p->src_addr);

			if (lp == NULL && *cfg.callsign && callsign_eq(p->src_addr, cfg.callsign))
				lp = rs_config_get_next(NULL);
			if (lp != NULL)
				cp = lp;
	/*		if (cp == NULL)
				cp = "All";*/
	// Print Port, src call, dest call
			if(Colored)
				tprintf("%-6s %s%-9s -> %s%-9s%s", cp, NodeColors.indicatif, p->src_addr, NodeColors.indicatif, p->dest_addr, ResetColor);
			else
			tprintf("%-6s %-9s -> %-9s", cp, p->src_addr, p->dest_addr);

			if (!strcmp(p->dest_addr, "*"))
			{
				cp = "Listening    ";
				for (j = 0; j< 18 ; j++)
					tprintf(" ");
				if(Colored)
					tprintf("%s%s%s%s\n", ResetColor, NodeColors.indicatif, cp, ResetColor);
				else
					tprintf(" %s\n",cp);
			continue;
			}

	// Digipeaters
			for (i=0 ; i< 2; i++)
			{
				if (i <= p->ndigi)
				{
					if (strncmp(p->digi_addr[i] , "*", 1) != 0)
						tprintf("%-9s", p->digi_addr[i]);
					else
					{
					for (j = 0; j< 9 ; j++)
						tprintf("°");
					}
				}
				else
					for (j = 0; j< 9 ; j++)
						tprintf(" ");
			}

	// Print AX25 connexions status
			switch (p->st)
			{
			case 0:
				cp = "Disconnected";
				if(Colored)
					tprintf(" %s%s%s%s", ResetColor, NodeColors.ax25_disconnected, cp, ResetColor);
				else
					tprintf(" %s",cp);
				break;
			case 1:
				cp = "Conn pending";
				if(Colored)
					tprintf(" %s%s%s%s", ResetColor, NodeColors.ax25_conn_pending, cp, ResetColor);
				else
					tprintf(" %s",cp);
				break;
			case 2:
				cp = "Disc pending";
				if(Colored)
					tprintf(" %s%s%s%s", ResetColor, NodeColors.ax25_disc_pending, cp, ResetColor);
				else
					tprintf(" %s",cp);
				break;
			case 3:
				cp = "Connected   ";
				if(Colored)
					tprintf(" %s%s%s%s", ResetColor, NodeColors.ax25_connected, cp, ResetColor);
				else
					tprintf(" %s",cp);
				break;
			case 4:
				cp = "Recovery    ";
				if(Colored)
					tprintf(" %s%s%s%s", ResetColor, NodeColors.ax25_recovery, cp, ResetColor);
				else
					tprintf(" %s",cp);
				break;
			default:
				cp = "Unknown     ";
				if(Colored)
					tprintf(" %s%s%s%s", ResetColor, NodeColors.ax25_unknown, cp, ResetColor);
				else
					tprintf(" %s",cp);
				break;
			}

	// Print ROSE connexions status

			if(node_is_connected(p->dest_addr)) {
				if(Colored)
	//				tprintf("%s %s%sConnected %s", ResetColor, F_DarkRed, B_DarkGreen, ResetColor);
					tprintf("%s %sConnected %s", ResetColor, NodeColors.rose_connected, ResetColor);
				else
					tprintf(" Connected");
			}
			else {
	//if(!node_is_connected(p->dest_addr))
				if(Colored)
					tprintf("%s %s--------- %s", ResetColor, NodeColors.rose_idle, ResetColor);
				else
					tprintf(" ---------");
			}

	// Print NetRom connexions status

			if(netrom_node_is_connected(p->dest_addr)) {
				if(Colored)
					tprintf(" %s %sConnected%s", ResetColor, NodeColors.netrom_connected, ResetColor);
				else
					tprintf("   Connected");
			}
	//if(!netrom_node_is_connected(p->dest_addr))
			else {
				if(Colored)
					tprintf("%s  %s---------%s", ResetColor, NodeColors.netrom_idle, ResetColor);
				else
					tprintf("   ---------");
			}

	//		tprintf("%s%s%s", ResetColor, B_Default, F_Default);
		
			if (is_sysop())
			{
				tprintf("    %02d/%02d %03lu/%03lu %03lu/%03lu %03d/%03d %03lu %5d %5d",
						p->vs < p->va ? p->vs - p->va + 8 : p->vs - p->va,
						p->window,
						p->t1timer/ HZ, p->t1 / HZ,
						p->t3timer/ HZ, p->t3 / HZ,
						p->n2count, p->n2, p->rtt/ HZ, p->sndq, p->rcvq);
			}
			tprintf("\n");
		} // cp != NULL
	}
	free_proc_ax25(list);

	first = 1;

	if ((rlist = read_proc_rs()) == NULL)
	{
		if (errno)
			node_perror("do_users: read_proc_rs ", errno);
		return 0;
	}

	if ((listv = read_proc_rs_neigh()) == NULL)
	{
		if (errno)
		{
			node_perror("do_users: read_proc_rs_neigh ", errno);
			return 0;
		}
	}

	for (rp = rlist; rp != NULL; rp = rp->next)
	{
		char neigh[20];

		if (argc > 1 && strcasecmp(argv[1], "*")
			&& !callsign_eq(rp->dest_call, argv[1])
			&& !callsign_eq(rp->src_call, argv[1]))
			continue;
		if ((argc < 2) && !strcmp(rp->dest_addr, "*"))
			continue;

		if ((wp_check_call(rp->src_call) != 0)
			&& (wp_check_call(rp->dest_call) != 0))
			continue;

		/*if (rp->lci >= 2048)*/
/*		if (rp->lci > ROSE_DEFAULT_MAXVC)
			continue;*/

		if (first)
		{
			first = 0;
			tprintf("\n");
			node_msg("Users - AX.25 Level 3 sessions :");
			if (Colored)	
				tprintf("%sCallsign%s  %sDNIC addr%s   <-> %sCallsign%s  %sDNIC addr%s   %sLCI Adjacent%s    %sAX.25 state%s", NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor);
			else
				tprintf
				("Callsign  DNIC addr   <-> Callsign  DNIC addr   LCI Adjacent    AX.25 state");
			if (is_sysop())
			{
				if (Colored)
					tprintf("    %sUnack Snd-Q Rcv-Q%s", NodeColors.en_tete, ResetColor);
				else
					tprintf("    Unack Snd-Q Rcv-Q");
			}
			tprintf("\n");
		}

		*neigh = '\0';
		for (pv = listv; pv != NULL; pv = pv->next)
			if (rp->neigh == (unsigned int)pv->addr)
				sprintf(neigh, "(%s)", pv->call);

		if (Colored)
		{
			tprintf("%s%-9s%s %-10s ",
					NodeColors.indicatif, rp->src_call, ResetColor,
					roseaddr(rp->src_addr));
			tprintf("<-> %s%-9s%s %-10s %03X %s%-11s%s ",
					NodeColors.indicatif, rp->dest_call, ResetColor,
					roseaddr(rp->dest_addr),
					rp->lci,
					NodeColors.indicatif, neigh, ResetColor);
		}
		else
		{
			tprintf("%-9s %-10s ", rp->src_call, roseaddr(rp->src_addr));
			tprintf("<-> %-9s %-10s %03X %-11s ",
					rp->dest_call, roseaddr(rp->dest_addr), rp->lci, neigh);
		}

		if (!strcmp(rp->dest_addr, "*"))
		{
			tprintf("Listening\n");
			continue;
		}
		switch (rp->st)
		{
		case 0:
			if (Colored)
				tprintf("%sDisconnected%s", NodeColors.ax25_disconnected, ResetColor);
			else
				cp = "Disconnected";
			break;
		case 1:
			if (Colored)
				tprintf("%sConn pending%s", NodeColors.ax25_conn_pending, ResetColor);
			else
				cp = "Conn pending";
			break;
		case 2:
			if (Colored)
				tprintf("%sDisc pending%s", NodeColors.ax25_disc_pending, ResetColor);
			else
				cp = "Disc pending";
			break;
		case 3:
			if (Colored)
				tprintf("%sConnected   %s", NodeColors.ax25_connected, ResetColor);
			else
				cp = "Connected   ";
			break;
		case 4:
			if (Colored)
				tprintf("%sRecovery    %s", NodeColors.ax25_recovery, ResetColor);
			else
				cp = "Recovery    ";
			break;
		default:
			if (Colored)
				tprintf("%sUnknown     %s", NodeColors.ax25_unknown, ResetColor);
			else
				cp = "Unknown     ";
			break;
		}
		if (!Colored)
			tprintf("%s", cp);
		if (is_sysop())
		{
			tprintf("   %02d   %5d %5d",
					rp->vs < rp->va ? rp->vs - rp->va + 8 : rp->vs - rp->va,
					rp->sndq, rp->rcvq);
		}
		tprintf("\n");
	}
	free_proc_rs(rlist);

	if ((tlist = read_proc_rs_routes()) == NULL)
	{
		if (errno)
		{
			node_perror("do_users: read_proc_rs_route ", errno);
			return 0;
		}
	}

	first = 1;

	for (tp = tlist; tp != NULL; tp = tp->next)
	{
		char nei1[20], nei2[20];

		if (argc > 1 && strcasecmp(argv[1], "*")
			&& !callsign_eq(tp->call1, argv[1])
			&& !callsign_eq(tp->call2, argv[1]))
			continue;

		/* Do not display no-peers */
		if ((argc < 2)
			&& (!strcmp(tp->address1, "*") || !strcmp(tp->address2, "*")))
			continue;

		if (first)
		{
			first = 0;
			tprintf("\n");
			node_msg("Users - AX.25 Level 3 transits :");
			if (Colored)	
				tprintf("%sCallsign%s  %sDNIC addr%s   %sLCI Adjacent%s   <-> %sCallsign%s  %sDNIC addr%s   %sLCI Adjacent%s\n", NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor);
			else
				tprintf
				("Callsign  DNIC addr   LCI Adjacent   <-> Callsign  DNIC addr   LCI Adjacent\n");
		}

		*nei1 = '\0';
		for (pv = listv; pv != NULL; pv = pv->next)
			if (tp->neigh1 == (unsigned int)pv->addr)
				sprintf(nei1, "(%s)", pv->call);

		*nei2 = '\0';
		for (pv = listv; pv != NULL; pv = pv->next)
			if (tp->neigh2 == (unsigned int)pv->addr)
				sprintf(nei2, "(%s)", pv->call);

		if (Colored)
		{
			tprintf("%s%-9s%s %-10s %03X %s%-11s%s",
					NodeColors.indicatif, tp->call1, ResetColor,
					roseaddr(tp->address1),
					tp->lci1,
					NodeColors.indicatif, nei1, ResetColor);
			tprintf("<-> %s%-9s%s %-10s %03X %s%s%s\n",
					NodeColors.indicatif, tp->call2, ResetColor,
					roseaddr(tp->address2),
					tp->lci2,
					NodeColors.indicatif, nei2, ResetColor);
		}
		else
		{
			tprintf("%-9s %-10s %03X %-11s",
					tp->call1, roseaddr(tp->address1), tp->lci1, nei1);
			tprintf("<-> %-9s %-10s %03X %s\n",
					tp->call2, roseaddr(tp->address2), tp->lci2, nei2);
		}

	}
	free_proc_rs_routes(tlist);
	free_proc_rs_neigh(listv);

	return 0;
}

int do_manage_routes(int argc, char **argv)
{
	int i;
	int s;
	int len;
	int action;
	char nodeaddr[11];
	struct rose_route_struct rs_node;
	struct proc_rs_nodes *listn;

	/* Check for SYSOP rights */
	if (!is_sysop())
	{
		node_msg("routes : sysop only command");
		return 0;
	}

	switch (*argv[1])
	{
	case 'a':
	case 'A':
		/* Add route */
		action = 'A';
		break;
	case 'd':
	case 'D':
		/* Delete route */
		action = 'D';
		break;
	default:
		return 0;
	}

	if (argc < 3)
	{
		node_msg("routes : address missing");
		return 0;
	}

	nodeaddr[10] = '\0';
	memset(&nodeaddr, '0', 10);

// pour le masque adresse
	len = strlen(argv[2]);

	if ((len < 4) || (len > 10) || (strspn(argv[2], "0123456789") != (size_t)len))
	{
		node_msg("routes : address error (4 to 10 digits)");
		return 0;
	}
	else
		snprintf(nodeaddr,len + 1, "%s", argv[2]);

	rs_node.mask = len;

	for (i = rs_node.mask; i < 10; i++)
		nodeaddr[i] = '0';

	if (argc < 4)
	{
		node_msg("routes : adjacent callsign missing");
		return 0;
	}

	if (rose_aton(nodeaddr, rs_node.address.rose_addr) != 0)
	{
		node_msg("do_manage_routes: invalid address %s", rs_node.address.rose_addr);
		return (0);
	}

	/* Search device for the adjacent */
	if ((listn = read_proc_rs_nodes()) == NULL)
	{
		node_msg("do_manage_routes: error read_proc_nr_node");
		return 0;
	}
	
	node_msg("do_manage_routes: address %s callsign %s", nodeaddr, argv[3]);

	if (ax25_aton_entry(argv[3], rs_node.neighbour.ax25_call) != 0)
	{
		node_msg("invalid callsign %s", argv[3]);
		return (0);
	}

	for (i = 0; (i + 4) < argc && i < AX25_MAX_DIGIS; i++)
	{
		if (ax25_aton_entry(argv[i + 4], rs_node.digipeaters[i].ax25_call) !=
			0)
		{
			node_msg("invalid callsign %s", argv[i + 4]);
			return (0);
		}
	}

	rs_node.ndigis = i;

	if ((s = socket(AF_ROSE, SOCK_SEQPACKET, 0)) < 0)
	{
		node_perror("do_manage_routes: socket", errno);
		return 0;
	}

	switch (action)
	{
	case 'A':
		if (ioctl(s, SIOCADDRT, &rs_node) == -1)
		{
			node_perror("cannot add this route", errno);
			close(s);
			return 0;
		}
		break;
	case 'D':
		if (ioctl(s, SIOCDELRT, &rs_node) == -1)
		{
			node_perror("cannot delete this route", errno);
			close(s);
			return 0;
		}
		break;
	}

	close(s);

	return 1;
}

int do_routes(int argc, char **argv)
{
	struct proc_rs_neigh *pv, *listv;
	struct proc_rs_nodes *pn, *listn;
/*	struct proc_ax25 *list;*/
	int i;
	int first = 1;
	int loopback = -1;
	char stradd[11];
	char *addr = NULL;
	wp_t *wpn = NULL;
	int nwp = 0;
	char *ccall;
/*	int len;
	cover_t *cl;
	addrp_t *al;*/


	if ((argc > 1) && (*argv[1] == '?'))
	{
		node_msg("usage : routes [dnic|address]");
		return (0);
	}

	if (argc > 1)
	{
		int len;

		len = strlen(argv[1]);

		if (len
			&& (strncasecmp(argv[1], "add", len) == 0
				|| strncasecmp(argv[1], "del", len) == 0))
			return do_manage_routes(argc, argv);

		if (((len != 4) && (len != 6) && (len != 10))
			|| (strspn(argv[1], "0123456789") != (size_t)len))
		{
			node_msg("routes : fpac dnic/address error");
			return (0);
		}
		stradd[0] = '\0';
		if (len == 6)
			strcpy(stradd, cfg.dnic);
		strcat(stradd, argv[1]);
		addr = stradd;
	}

	/* Coverage */
	if (cfg.cover)
	{
		cover_t *cl;
		if (Colored)
			tprintf("%sCoverage%s\n", NodeColors.en_tete, ResetColor);
		else
			tprintf("Coverage\n");
		for (i = 0, cl = cfg.cover; cl; cl = cl->next)
		{
			if (Colored)
				tprintf("%s%s,%s%s %c", NodeColors.adresse, cfg.dnic, cl->addr, ResetColor,
						((i + 1) % 4) ? ' ' : '\n');
			else
				tprintf("%s,%s %c", cfg.dnic, cl->addr,
						((i + 1) % 4) ? ' ' : '\n');
			++i;
		}
		if ((i % 4) != 0)
			tprintf("\n");
		tprintf("\n");
	}

	/* Ports */
	if (cfg.addrp)
	{
		addrp_t *al;
		if (Colored)
			node_msg("%sAddress      Port    Description%s", NodeColors.en_tete, ResetColor);
		else
			node_msg("Address      Port    Description");
		for (al = cfg.addrp; al; al = al->next)
		{
			if (Colored)
				tprintf("%s%s,%s%s  %-6s  %s\n",
						NodeColors.adresse, cfg.dnic, al->addr, ResetColor,
						(al->port[0]) ? al->port : "?",
						(al->port[0]) ? ax25_config_get_desc(al->port) : "None");
			else
				tprintf("%s,%s  %-6s  %s\n",
						cfg.dnic,
						al->addr,
						(al->port[0]) ? al->port : "?",
						(al->port[0]) ? ax25_config_get_desc(al->port) : "None");
		}
		tprintf("\n");
	}

	/* White Pages node list, for the Connect column (L2 callsigns).
	 * nwp is an input of wp_get_list(): the maximum number of records
	 * (same value as the Nodes command), and the number read on return. */
	nwp = 100;
	if (wp_open("NODE") == 0)
	{
		if (wp_get_list(&wpn, &nwp, WP_NODE_FLAG, "*") == -1)
			nwp = 0;
		wp_close();
	}
	else
		nwp = 0;

	/* Routes */
	if ((listv = read_proc_rs_neigh()) == NULL)
	{
		if (errno)
			node_perror("do_routes: read_proc_rs_neigh", errno);
		wp_free_list(&wpn);
		return 0;
	}

	/* Search the node number of the loopback */
	for (pv = listv; pv != NULL; pv = pv->next)
		if (strncmp(pv->call, "RSLOOP", 6) == 0)
		{
			loopback = pv->addr;
			break;
		}

	if ((listn = read_proc_rs_nodes_ordered()) == NULL)
	{
		if (errno)
			node_perror("do_routes: read_proc_rs_nodes", errno);
		free_proc_rs_neigh(listv);
		wp_free_list(&wpn);
		return 0;
	}
/*
	if ((list = read_proc_ax25()) == NULL) 
	{
		if (errno)
			node_perror("do_routes: read_proc_ax25", errno);
		return 0;
	}
*/
	first = 1;

	for (pn = listn; pn != NULL; pn = pn->next)
	{
		if (pn->neigh1 == (unsigned int)loopback)
			continue;

		/* routes <dnic>: every route inside this DNIC; routes <address>:
		 * every route covering this address */
		if ((addr) && (strncmp(addr, pn->address,
				       (strlen(addr) < (size_t)pn->mask) ? strlen(addr) : (size_t)pn->mask) != 0))
			continue;

		if (pn->address[0] == '*')
			continue;

		if (first)
		{
			if (Colored)
				node_msg("ROSE routes :\n%sDNIC Address%s %sConnect  %s %sPrimary   Route%s  | %s1st Alt   Route%s  | %s2nd Alt   Route%s  |", NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor);
			else
				node_msg("ROSE routes :\nDNIC Address Connect   Primary   Route  | 1st Alt   Route  | 2nd Alt   Route  |");
			first = 0;
		}

		/*
		 * All kernel routes are listed, in kernel order (most specific
		 * first), including the exact 10 digit routes to adjacent nodes
		 * which were hidden before 4.1.6-rc3.
		 */
		{
		for (i = pn->mask; i < 10; i++)
			pn->address[i] = '.';

		/* Connect callsign: only an exact route designates one node */
		ccall = (pn->mask == 10) ? wp_connect_call(wpn, nwp, pn->address) : NULL;
		tprintf("%s ", roseaddr(pn->address));
		if (Colored)
			tprintf("%s%-9s%s ", NodeColors.indicatif, ccall ? ccall : (pn->mask == 10 ? "?" : ""), ResetColor);
		else
			tprintf("%-9s ", ccall ? ccall : (pn->mask == 10 ? "?" : ""));
		for (pv = listv; pv != NULL; pv = pv->next)
		{
			if (pn->neigh1 == (unsigned int)pv->addr)
			{
				if(Colored) 
					tprintf("%s %-9s%s", NodeColors.indicatif, pv->call, ResetColor);
				else
					tprintf(" %-9s", pv->call);
				if (!strcmp("yes",pv->restart)) {
					if(Colored) 
//						tprintf("%s%s Opened %s|",F_DarkRed,B_DarkGreen,ResetColor);
						tprintf("%s Opened %s|", NodeColors.ind_oui, ResetColor);
					else
						tprintf(" Opened |");
				}
				else {
					if(Colored)
//						tprintf("%s%s Closed %s|",F_DarkGreen,B_DarkRed,ResetColor);
						tprintf("%s Closed %s|", NodeColors.ind_non, ResetColor);
					else
						tprintf(" Closed |");
				}
			}
		}
		for (pv = listv; pv != NULL; pv = pv->next)
		{
			if (pn->neigh2 == (unsigned int)pv->addr)
			{
				if(Colored) 
					tprintf("%s %-9s%s", NodeColors.indicatif, pv->call, ResetColor);
				else
					tprintf(" %-9s", pv->call);
				if (!strcmp("yes",pv->restart)) {
					if(Colored)
//						tprintf("%s%s Opened %s|",F_DarkRed,B_DarkGreen,ResetColor);
						tprintf("%s Opened %s|", NodeColors.ind_oui, ResetColor);
					else
						tprintf(" Opened |");
				}
				else {
					if(Colored) 
//						tprintf("%s%s Closed %s|",F_DarkGreen,B_DarkRed,ResetColor);
						tprintf("%s Closed %s|", NodeColors.ind_non, ResetColor);
					else
						tprintf(" Closed |");
				}
			}
		}
		for (pv = listv; pv != NULL; pv = pv->next)
		{
			if (pn->neigh3 == (unsigned int)pv->addr)
			{
				if(Colored) 
					tprintf("%s %-9s%s", NodeColors.indicatif, pv->call, ResetColor);
				else
					tprintf(" %-9s", pv->call);
				if (!strcmp("yes",pv->restart)) {
					if(Colored) 
//						tprintf("%s%s Closed %s|",F_DarkGreen,B_DarkRed,ResetColor);
						tprintf("%s Opened %s|", NodeColors.ind_oui, ResetColor);
					else
						tprintf(" Opened |");
				}
				else {
					if(Colored) 
//						tprintf("%s%s Closed %s|",F_DarkGreen,B_DarkRed,ResetColor);
						tprintf("%s Closed %s|", NodeColors.ind_non, ResetColor);
					else
						tprintf(" Closed |");
				}
			}
		}
		tprintf("\n");
		}
	}
	free_proc_rs_neigh(listv);
	free_proc_rs_nodes(listn);
/*	free_proc_ax25(list) ;*/

	if (addr && first)
		node_msg("No route to %s", roseaddr(addr));
	else if (addr && strlen(addr) == 10)
	{
		/* Route the kernel will really use to connect to this address */
		char entry[11], call[10];
		int mask;

		if (rose_predict_route(addr, entry, &mask, call) != 0)
			node_msg(T("No usable neighbour to %s"), roseaddr(addr));
		else if (strncmp(call, "RSLOOP", 6) == 0)
			node_msg(T("%s is this node"), roseaddr(addr));
		else
		{
			char dest[12];

			for (i = mask; i < 10; i++)
				entry[i] = '.';
			/* roseaddr() returns a static buffer: copy the first one */
			strcpy(dest, roseaddr(addr));
			node_msg(T("Route used to %s : %s via %s"), dest, roseaddr(entry), call);
			/* The callsign a user must type to reach this node */
			if ((ccall = wp_connect_call(wpn, nwp, addr)) != NULL)
				node_msg(T("To connect : C %s"), ccall);
			else
				node_msg(T("Connect callsign of %s unknown (not in the White Pages)"), dest);
		}
	}
	wp_free_list(&wpn);

	return 0;
}

int do_manage_links(int argc, char **argv)
{
	int i;
	int s;
	int action;
	char *dev;
	struct rose_route_struct rs_node;
	struct proc_rs_neigh *pv, *listv;

	/* Check for SYSOP rights */
	if (!is_sysop())
	{
		node_msg("routes : sysop only command");
		return 0;
	}

	switch (*argv[1])
	{
	case 'a':
	case 'A':
		/* Add route */
		action = 'A';
		break;
	case 'd':
	case 'D':
		/* Delete route */
		action = 'D';
		break;
	default:
		return 0;
	}

	if (argc < 3)
	{
		node_msg("links : port missing");
		return 0;
	}

	if ((dev = ax25_config_get_dev(argv[2])) == NULL)
	{
		node_msg("invalid port name %s", argv[2]);
		return (0);
	}

	if (argc < 4)
	{
		node_msg("links : adjacent callsign missing");
		return 0;
	}

	/* Search device for the adjacent */
	if ((listv = read_proc_rs_neigh()) == NULL)
	{
		node_msg("do_manage_routes: error read_proc_nr_neigh");
		return 0;
	}

	for (pv = listv; pv != NULL; pv = pv->next)
		if (callsign_eq(argv[3], pv->call))
			break;

	free_proc_rs_neigh(listv);

	if ((action == 'A') && pv)
	{
		node_msg("adjacent %s already exists", argv[3]);
		return 0;
	}

	rs_node.mask = 10;

	if (rose_aton("0000000000", rs_node.address.rose_addr) != 0)
	{
		node_msg("do_manage_links: invalid address");
		return (0);
	}

	strcpy(rs_node.device, dev);

	if (ax25_aton_entry(argv[3], rs_node.neighbour.ax25_call) != 0)
	{
		node_msg("invalid callsign %s", argv[3]);
		return (0);
	}

	for (i = 0; (i + 4) < argc && i < AX25_MAX_DIGIS; i++)
	{
		if (ax25_aton_entry(argv[i + 4], rs_node.digipeaters[i].ax25_call) !=
			0)
		{
			node_msg("invalid callsign %s", argv[i + 4]);
			return (0);
		}
	}

	rs_node.ndigis = i;

	if ((s = socket(AF_ROSE, SOCK_SEQPACKET, 0)) < 0)
	{
		node_perror("do_manage_routes: socket", errno);
		return 0;
	}

	switch (action)
	{
	case 'A':
		if (ioctl(s, SIOCADDRT, &rs_node) == -1)
		{
			node_perror("cannot add this link", errno);
			close(s);
			return 0;
		}
		break;
	case 'D':
		if (ioctl(s, SIOCDELRT, &rs_node) == -1)
		{
			node_perror("cannot delete this link", errno);
			close(s);
			return 0;
		}
		break;
	}

	close(s);

	return 1;
}

int do_links(int argc, char **argv)
{
	struct proc_rs_neigh *nlist;
	struct proc_nr *nrlist;
	struct proc_ax25 *p, *list;
	char *cp = NULL;
	char *pdev= NULL;

	if (argc > 1)
	{
		int len;

		len = strlen(argv[1]);

		if (len
			&& (strncasecmp(argv[1], "add", len) == 0
				|| strncasecmp(argv[1], "del", len) == 0))
			return do_manage_links(argc, argv);
	}

	if ((argc > 1) && (*argv[1] == '?'))
	{
		node_msg("usage : links");
		return (0);
	}

	if ((nlist = read_proc_rs_neigh()) == NULL)
	{
		if ((nrlist = read_proc_nr()) == NULL)
			node_msg("No adjacent nodes");
		return 0;
	}

	if (rs_config_get_next(cp) == NULL)
		(void)rs_config_load_ports();
	list = read_proc_ax25();
	if (Colored)
		node_msg("Adjacent Nodes Links:\n%sCallsign%s  %sStatus%s       %sDev%s    %sIface%s  %sPort%s", NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor);
	else
		node_msg("Adjacent Nodes Links:\nCallsign  Status       Dev    Iface  Port");
	/* "nodes" */
	for (p = list; p != NULL; p = p->next)
	{
		if (wp_check_call(p->dest_addr) !=0)
			continue;

		const char *state_color;
		switch (p->st)
		{
		case 0:
			cp = "Disconnected";
			state_color = NodeColors.ax25_disconnected;
			break;
		case 1:
			cp = "Conn pending";
			state_color = NodeColors.ax25_conn_pending;
			break;
		case 2:
			cp = "Disc pending";
			state_color = NodeColors.ax25_disc_pending;
			break;
		case 3:
			cp = "Connected   ";
			state_color = NodeColors.ax25_connected;
			break;
		case 4:
			cp = "Recovery    ";
			state_color = NodeColors.ax25_recovery;
			break;
		default:
			cp = "Unknown     ";
			state_color = NodeColors.ax25_unknown;
			break;
		}
		if ((pdev = ax25_config_get_name(p->dev)) != NULL) {

		if(Colored) {

		if (netrom_node_is_connected(p->dest_addr) && (p->st > 0))
			tprintf("%s%-9s%s %s%-12s%s %-6s %-6s %-6s\n",
					NodeColors.indicatif,
					p->dest_addr, ResetColor,
					state_color,
					cp, ResetColor,
					nr_config_get_dev(nr_config_get_name(p->dest_addr)),
					p->dev,
					ax25_config_get_name(p->dev)
					);
		if (node_is_connected(p->dest_addr))
			tprintf("%s%-9s%s %s%-12s%s %-6s %-6s %-6s\n",
					NodeColors.indicatif,
					p->dest_addr, ResetColor,
					state_color,
					cp, ResetColor,
					rs_config_get_dev(rs_config_get_name(p->dest_addr)),
					p->dev,
					ax25_config_get_name(p->dev)
					);
		}
		else {
			
		if (netrom_node_is_connected(p->dest_addr) && (p->st > 0))
			tprintf("%-9s %-12s %-6s %-6s %-6s\n",
					p->dest_addr,
					cp,
					nr_config_get_dev(nr_config_get_name(p->dest_addr)),
					p->dev,
					ax25_config_get_name(p->dev)
					);
		if (node_is_connected(p->dest_addr))
			tprintf("%-9s %-12s %-6s %-6s %-6s\n",
					p->dest_addr,
					cp,
					rs_config_get_dev(rs_config_get_name(p->dest_addr)),
					p->dev,
					ax25_config_get_name(p->dev)
					);
		}
		}
		}

	free_proc_ax25(list);
	free_proc_rs_neigh(nlist);
/*	free_proc_nr(nrlist); */

	return 0;
}

/* Comparateurs pour qsort */
static int cmp_alpha(const void *a, const void *b)
{
    const struct proc_nr_nodes *na = *(const struct proc_nr_nodes **)a;
    const struct proc_nr_nodes *nb = *(const struct proc_nr_nodes **)b;
    return strcmp(na->call, nb->call);
}

static int cmp_mnemonic(const void *a, const void *b)
{
    const struct proc_nr_nodes *na = *(const struct proc_nr_nodes **)a;
    const struct proc_nr_nodes *nb = *(const struct proc_nr_nodes **)b;
    return strcmp(na->alias, nb->alias);
}

static int cmp_qual(const void *a, const void *b)
{
    const struct proc_nr_nodes *na = *(const struct proc_nr_nodes **)a;
    const struct proc_nr_nodes *nb = *(const struct proc_nr_nodes **)b;
    if (nb->qual1 > na->qual1) return  1;
    if (nb->qual1 < na->qual1) return -1;
    return 0;
}

/* sort_order : 'a' = callsign alphabétique
**              'm' = alias alphabétique
**              autre = qualité décroissante
*/
struct proc_nr_nodes *sort_proc_nr_nodes(struct proc_nr_nodes *list, char sort_order)
{
    if (!list || !list->next)
        return list;

    int n = 0;
    struct proc_nr_nodes *p;
    for (p = list; p; p = p->next)
        n++;

    struct proc_nr_nodes **arr = malloc(n * sizeof(*arr));
    if (!arr)
        return list;

    int i = 0;
    for (p = list; p; p = p->next)
        arr[i++] = p;

    int (*cmp)(const void *, const void *);
    switch (sort_order) {
        case 'a': cmp = cmp_alpha;    break;
        case 'm': cmp = cmp_mnemonic; break;
        default:  cmp = cmp_qual;     break;
    }
    qsort(arr, n, sizeof(*arr), cmp);

    for (i = 0; i < n - 1; i++)
        arr[i]->next = arr[i + 1];
    arr[n - 1]->next = NULL;

    list = arr[0];
    free(arr);
    return list;
}


/*
static int rose_sort(const void *a, const void *b)
{
	char *c1 = ((wp_t *) a)->address.srose_call.ax25_call;
	char *c2 = ((wp_t *) b)->address.srose_call.ax25_call;

	return memcmp(c1, c2, 7);
}
*/

int do_rose(int argc, char **argv)
{
/*	wp_t wpu, *wp = NULL;*/
	wp_t *wp = NULL;
	int i;
/*	int first;*/
	int nb = 100;
	int ret = -1;
/*	char *country, *addr, *call;
	ax25_address address;
*/

	if (wp_open("NODE") != 0) {
		tprintf ("No FPAC node list - fpacwpd is probably down\n");
		return (0);
	} 

	if (argc == 1)
	{
		node_msg("FPAC Nodes:");
		if (Colored)
			node_msg("%sConnect%s   %sDNIC addr%s    %sConnect%s   %sDNIC addr%s    %sConnect%s   %sDNIC addr%s",
					NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor,
					NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor,
					NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor);
		else
			tprintf ("Connect   DNIC addr    Connect   DNIC addr    Connect   DNIC addr\n");
		if (wp_get_list(&wp, &nb, WP_NODE_FLAG, "*") != -1)
		{
			for (i = 0; i < nb; i++)
			{
				if (Colored)
					tprintf("%s%9.9s%s %s %c",
							NodeColors.indicatif,
							ax25_ntoa(&wp[i].address.srose_call),
							ResetColor,
							fpac2asc(&wp[i].address.srose_addr),
							((i + 1) % 3) ? ' ' : '\n');
				else
					tprintf("%9.9s %s %c",
							ax25_ntoa(&wp[i].address.srose_call),
							fpac2asc(&wp[i].address.srose_addr),
							((i + 1) % 3) ? ' ' : '\n');
			}
			if ((i % 3) != 0)
				tprintf("\n");
			ret = 0;
		}

		wp_free_list(&wp);
	}
	else if (strpbrk(argv[1], "*?&=#@"))
	{
		node_msg("FPAC Nodes:");
		if (wp_get_list(&wp, &nb, WP_NODE_FLAG, "*") != -1)
		{
			for (i = 0; i < nb; i++)
			{
			char *country;
			char *addr;
			char *call = ax25_ntoa(&wp[i].address.srose_call);

				if (!strmatch(call, argv[1]))
					continue;

				addr = fpac2asc(&wp[i].address.srose_addr);
				country = dnic2des(addr);

				if (Colored)
					tprintf("%s%9.9s%s  %s  %3s  %s  %s\n",
							NodeColors.indicatif, call, ResetColor,
							addr,
							(country) ? country : "", wp[i].locator, wp[i].city);
				else
					tprintf("%9.9s  %s  %3s  %s  %s\n", call, addr,
							(country) ? country : "", wp[i].locator, wp[i].city);
				ret = 0;
			}
		}

		wp_free_list(&wp);
	}
	else if (strpbrk(argv[1], "-"))
	{
	int first = 1;

		for (i = 1; i < argc; i++)
		{
			ax25_address address;
			wp_t wpu;

			ax25_aton_entry(argv[i], address.ax25_call);
			if (wp_get(&address, &wpu) != -1)
			{
			char *addr = fpac2asc(&wpu.address.srose_addr);
			char *country = dnic2des(addr);

				if (!wpu.is_node)
					continue;
				if (first)
				{
					node_msg("FPAC Nodes:");
					first = 0;
				}

				if (Colored)
					tprintf("%s%9.9s%s  %s  %3s  %s  %s\n",
							NodeColors.indicatif,
							ax25_ntoa(&wpu.address.srose_call), ResetColor,
							addr,
							(country) ? country : "", wpu.locator, wpu.city);
				else
					tprintf("%9.9s  %s  %3s  %s  %s\n",
							ax25_ntoa(&wpu.address.srose_call), addr,
							(country) ? country : "", wpu.locator, wpu.city);
				*argv[i] = '\0';
				ret = 0;
			}
		}
	}
	else
	{
		node_msg("FPAC Nodes:");
		if (wp_get_list(&wp, &nb, WP_NODE_FLAG, "*") != -1)
		{
			for (i = 0; i < nb; i++)
			{
			char *country;
			char *addr;
			char *call = ax25_ntoa(&wp[i].address.srose_call);

				if (!callmatch(call, argv[1]))
					continue;

				addr = fpac2asc(&wp[i].address.srose_addr);
				country = dnic2des(addr);

				if (Colored)
					tprintf("%s%9.9s%s  %s  %3s  %s  %s\n",
							NodeColors.indicatif, call, ResetColor,
							addr,
							(country) ? country : "", wp[i].locator, wp[i].city);
				else
					tprintf("%9.9s  %s  %3s  %s  %s\n",
							call, addr,
							(country) ? country : "", wp[i].locator, wp[i].city);
				ret = 0;
			}
		}


		wp_free_list(&wp);
	}

	wp_close();

	return ret;
}

/*
 * F6BVP 2026-09-19: colour of a NetRom quality according to whether the node
 * can really be reached, not only to the advertised quality.
 *
 * The kernel (nr_route_frame(), net/netrom/nr_route.c) silently drops every
 * frame for a node whose active route index "which" is not below its number
 * of routes -- /proc/net/nr_nodes prints which + 1 in column "w". That is
 * what nr_link_failed() leaves behind after link_fails_count link failures
 * for a node having a single route: the table still shows quality 247-255
 * but nothing is ever transmitted.
 *
 *   red    : quality 0, or 120 (static rc.netrom value never refreshed), or
 *            invalid active route (w > n), or neighbour of the route with
 *            "failed" >= link_fails_count, or whose AX.25 link is stuck
 *            awaiting connection (unanswered SABM)
 *   green  : quality > 200 and the neighbour has no recorded failure
 *   yellow : anything else (quality 1-200, or 1 failure below the limit)
 */
struct nr_failed { int addr; int failed; };

static int nr_failed_load(struct nr_failed **out)
{
	FILE *fp;
	char line[256], call[16], dev[16];
	int addr, qual, lock, cnt, failed;
	int n = 0, cap = 64;
	struct nr_failed *a = malloc(cap * sizeof(*a));

	*out = a;
	if (a == NULL)
		return 0;
	if ((fp = fopen("/proc/net/nr_neigh", "r")) == NULL)
		return 0;
	if (fgets(line, sizeof(line), fp) == NULL)	/* header */
	{
		fclose(fp);
		return 0;
	}
	while (fgets(line, sizeof(line), fp) != NULL)
	{
		if (sscanf(line, "%d %15s %15s %d %d %d %d",
			   &addr, call, dev, &qual, &lock, &cnt, &failed) != 7)
			continue;
		if (n == cap)
		{
			struct nr_failed *b = realloc(a, 2 * cap * sizeof(*a));
			if (b == NULL)
				break;
			a = b;
			*out = a;
			cap *= 2;
		}
		a[n].addr = addr;
		/* a link stuck awaiting connection counts as a failure: the
		 * kernel only raises "failed" once all N2 retries are used */
		a[n].failed = nr_link_pending(call, NULL, NULL) ? 999 : failed;
		n++;
	}
	fclose(fp);
	return n;
}

static int nr_failed_of(const struct nr_failed *a, int n, int addr)
{
	int i;

	for (i = 0; i < n; i++)
		if (a[i].addr == addr)
			return a[i].failed;
	return 0;
}

static int nr_fails_limit(void)
{
	FILE *fp = fopen("/proc/sys/net/netrom/link_fails_count", "r");
	int v = 2;

	if (fp != NULL)
	{
		if (fscanf(fp, "%d", &v) != 1)
			v = 2;
		fclose(fp);
	}
	return (v > 0) ? v : 2;
}

static const char *nr_qcolor(int qual, int failed, int limit, int invalid)
{
	if (invalid || qual == 0 || qual == 120 || failed >= limit)
		return NodeColors.qualite_nulle;
	if (qual > 200 && failed == 0)
		return NodeColors.qualite_bonne;
	return NodeColors.qualite_moyenne;
}

/* Neighbour number of the route the kernel currently uses (0 if none). */
static int nr_active_addr(const struct proc_nr_nodes *p)
{
	return (p->w == 1) ? p->addr1 : (p->w == 2) ? p->addr2 : (p->w == 3) ? p->addr3 : 0;
}

int do_netrom(int argc, char **argv)
{
	struct proc_nr_nodes *p, *list;
	struct proc_nr_neigh *np, *nlist;
	int i = 0;
	char sort_order = 0;   /* 0 = qualité, 'a' = callsign, 'm' = alias mnemonic */
	int first;
	int fpac;
	struct nr_failed *nfa = NULL;
	int nfn = 0, nr_lim = 2;
	if ((argc > 1) && (*argv[1] == '?') && (strlen(argv[1]) == 1))
	{
		node_msg("usage : ne (<a> call order <m> alias order)");
		return (0);
	}
	
	fpac = 0;
	
	if ((list = read_proc_nr_nodes()) == NULL)
	{
		node_msg("No NetRom Nodes - NetRom is probably not configured on this system");
		if (fpac == -1)
			node_msg("No node");
		return 0;
	}
	/* F6BVP sort order option : 'a' = callsign, 'm' = alias mnemonic */
	if (argc == 2)
	{
		if (*argv[1] == 'a' || *argv[1] == 'm')
		{
			sort_order = *argv[1];
			argc--;
		}
	}
	list = sort_proc_nr_nodes(list, sort_order);
	/* "nodes" */
	if (argc == 1)
	{
		if (fpac)
			tprintf("\n");
		node_msg("NetRom Nodes:");
		if (Colored)
			node_msg(" %sAlias:Callsign Qual.%s   %sAlias:Callsign Qual.%s   %sAlias:Callsign Qual.%s",
					NodeColors.en_tete, ResetColor,
					NodeColors.en_tete, ResetColor,
					NodeColors.en_tete, ResetColor);
		else
			node_msg(" Alias:Callsign Qual.   Alias:Callsign Qual.   Alias:Callsign Qual.");
		nfn = nr_failed_load(&nfa);
		nr_lim = nr_fails_limit();
		for (p = list; p != NULL; p = p->next)
		{
			char sep = ((i + 1) % 3) ? ' ' : '\n';
			if (Colored)
			{
				/* F6BVP 2026-09-19: colour follows reachability, see
				 * nr_qcolor() -- same rule in the three sort orders and
				 * in "ne *". Neighbour of the ACTIVE route (column w). */
				int inv = (p->n > 0) && (p->w < 1 || p->w > p->n);
				const char *qcol = nr_qcolor(p->qual1,
						(p->n > 0 && !inv) ? nr_failed_of(nfa, nfn, nr_active_addr(p)) : 0,
						nr_lim, inv);
				if (sort_order == 'a')
				{
					/* tri callsign : callsign en vert */
					tprintf("%6s%c%s%-9s%s(%s%-3d%s) %c",
							!strcmp(p->alias, "*") ? "" : p->alias,
							!strcmp(p->alias, "*") ? ' ' : ':',
							NodeColors.indicatif, p->call, ResetColor,
							qcol, p->qual1, ResetColor, sep);
				}
				else if (sort_order == 'm')
				{
					/* tri alias : alias en vert */
					if (!strcmp(p->alias, "*"))
						tprintf("       %-9s(%s%-3d%s) %c",
								p->call, qcol, p->qual1, ResetColor, sep);
					else
						tprintf("%s%6s%s:%-9s(%s%-3d%s) %c",
								NodeColors.indicatif, p->alias, ResetColor,
								p->call, qcol, p->qual1, ResetColor, sep);
				}
				else
				{
					/* tri qualité (défaut) : qualité selon seuil seulement */
					tprintf("%-16.16s(%s%-3d%s) %c",
							print_node(p->alias, p->call),
							qcol, p->qual1, ResetColor,
							sep);
				}
			}
			else
			{
				tprintf("%-16.16s(%-3d) %c",
						print_node(p->alias, p->call), p->qual1, sep);
			}
			++i;
		}
		if ((i % 3) != 0)
			tprintf("\n");
		free(nfa);
		free_proc_nr_nodes(list);
		return 0;
	}
	if ((nlist = read_proc_nr_neigh()) == NULL)
	{
		if (!fpac)
			 node_msg("No node");
		free_proc_nr_nodes(list);
		return 0;
	}
	/* "nodes *" */
	if (*argv[1] == '*')
	{
		if (fpac)
			tprintf("\n");
		node_msg("NetRom Nodes:");
		if (Colored)
			tprintf("%sNode             %s  %sQuality%s %sObsolescence%s %sPort  %s %sNeighbour%s\n",
					NodeColors.en_tete, ResetColor,
					NodeColors.en_tete, ResetColor,
					NodeColors.en_tete, ResetColor,
					NodeColors.en_tete, ResetColor,
					NodeColors.en_tete, ResetColor);
		else
			tprintf("Node              Quality Obsolescence Port   Neighbour\n");
		nfn = nr_failed_load(&nfa);
		nr_lim = nr_fails_limit();
		for (p = list; p != NULL; p = p->next)
		{
			int inv = (p->n > 0) && (p->w < 1 || p->w > p->n);
			if (Colored)
				tprintf("%s%-16.16s%s  ", NodeColors.indicatif,
						print_node(p->alias, p->call), ResetColor);
			else
				tprintf("%-16.16s  ", print_node(p->alias, p->call));

			if (p->n == 0)		/* local node */
			{
				const char *qcol = Colored
					? nr_qcolor(p->qual1, 0, nr_lim, 0)
					: "";
				tprintf("%s%-7d%s %-12d\n",
						qcol, p->qual1, Colored ? ResetColor : "",
						p->obs1);
			}
			else
			if ((np = find_neigh(p->addr1, nlist)) != NULL)
			{
				const char *qcol = Colored
					? nr_qcolor(p->qual1, nr_failed_of(nfa, nfn, p->addr1), nr_lim, inv)
					: "";
				tprintf("%s%-7d%s %-12d %-6s %s%s%s\n",
						qcol, p->qual1, Colored ? ResetColor : "",
						p->obs1,
						ax25_config_get_name(np->dev),
						Colored ? NodeColors.indicatif : "",
						np->call,
						Colored ? ResetColor : "");
			}
			if (p->n > 1 && (np = find_neigh(p->addr2, nlist)) != NULL)
			{
				const char *qcol = Colored
					? nr_qcolor(p->qual2, nr_failed_of(nfa, nfn, p->addr2), nr_lim, inv)
					: "";
				tprintf("                  ");
				tprintf("%s%-7d%s %-12d %-6s %s%s%s\n",
						qcol, p->qual2, Colored ? ResetColor : "",
						p->obs2,
						ax25_config_get_name(np->dev),
						Colored ? NodeColors.indicatif : "",
						np->call,
						Colored ? ResetColor : "");
			}
			if (p->n > 2 && (np = find_neigh(p->addr3, nlist)) != NULL)
			{
				const char *qcol = Colored
					? nr_qcolor(p->qual3, nr_failed_of(nfa, nfn, p->addr3), nr_lim, inv)
					: "";
				tprintf("                  ");
				tprintf("%s%-7d%s %-12d %-6s %s%s%s\n",
						qcol, p->qual3, Colored ? ResetColor : "",
						p->obs3,
						ax25_config_get_name(np->dev),
						Colored ? NodeColors.indicatif : "",
						np->call,
						Colored ? ResetColor : "");
			}
		}
		free(nfa);
		free_proc_nr_nodes(list);
		free_proc_nr_neigh(nlist);
		return 0;
	}
	for (first = 1, i = 1; i < argc; i++)
	{
		if (*argv[i] == '\0')
			continue;
		/* "nodes <node>" */
		p = find_node(argv[i], list);
		if (p != NULL)
		{
			if (first)
			{
				if (fpac)
					tprintf("\n");
				tprintf("Routes to        Which Quality Obsolescence Port   Neighbour\n");
				first = 0;
			}
			if ((np = find_neigh(p->addr1, nlist)) != NULL)
			{
				tprintf("%-16s %c     %-7d %-12d %-6s %s\n",
						print_node(p->alias, p->call),
						p->w == 1 ? '>' : ' ',
						p->qual1, p->obs1,
						ax25_config_get_name(np->dev), np->call);
			}
			/* F6BVP 2026-09-19: the 2nd and 3rd route lines were printed
			 * without the 16-character node column, so they came out
			 * shifted to the left under the wrong headings; and the 3rd
			 * route was looked up for any node with more than one route
			 * (p->n > 1 instead of p->n > 2). */
			if (p->n > 1 && (np = find_neigh(p->addr2, nlist)) != NULL)
			{
				tprintf("%-16s %c     %-7d %-12d %-6s %s\n", "",
						p->w == 2 ? '>' : ' ',
						p->qual2, p->obs2,
						ax25_config_get_name(np->dev), np->call);
			}
			if (p->n > 2 && (np = find_neigh(p->addr3, nlist)) != NULL)
			{
				tprintf("%-16s %c     %-7d %-12d %-6s %s\n", "",
						p->w == 3 ? '>' : ' ',
						p->qual3, p->obs3,
						ax25_config_get_name(np->dev), np->call);
			}
			if (p->n > 0 && (p->w < 1 || p->w > p->n))
				tprintf(T("%-16s   Invalid active route (%d of %d): the kernel forwards nothing\n"),
						"", p->w, p->n);
			*argv[i] = '\0';
		}
	}
	free_proc_nr_nodes(list);
	free_proc_nr_neigh(nlist);
	first = 1;
	for (i = 1; i < argc; i++)
	{
		if (*argv[i])
		{
			if (first)
			{
				if (fpac)
					tprintf("\n");
				first = 0;
			}
			node_msg("No such node %s", argv[i]);
		}
	}
	return 0;
}


/*
 * by Heikki Hannikainen <hessu@pspt.fi> 
 * The following was mostly learnt from the procps package and the
 * gnu sh-utils (mainly uname).
 */

int do_status(int argc, char **argv)
{
	int upminutes, uphours, updays;
	double uptime_secs, idle_secs;
	double av[3];
/*	unsigned *mem;*/
	char *mem;
	unsigned int memtotal, memfree, buffers, cached, swaptotal, swapcached, swapfree;
	struct utsname name;
	time_t t;
	int n;
	int loopback = -1;
#define MAX_ROW 30
	char memo[MAX_ROW][17];			/* memory field label */ 
  	unsigned int value[MAX_ROW];		/* amount of memory   */
	char *p;	
	char kb[3];
	int i, k, l;
	struct proc_ax25 *axp, *axlist;
	struct proc_rs_route *tp, *tlist;
	struct proc_rs *rp, *rlist;
	struct proc_rs_nodes *rsno, *rsnolist;
	struct proc_rs_neigh *rsne, *rsnelist;
	port_t *pp;
	char userports[256];
	/* cfg_t cfg; -- supprimé : on utilise le cfg global déjà chargé au démarrage */

	memtotal = memfree = buffers = cached = swaptotal = swapcached = swapfree = 0;

	if ((argc > 1) && (*argv[1] == '?'))
	{
		node_msg("usage : status");
		return (0);
	}

	/* cfg_open(&cfg) supprimé : évite double appel + corruption heap + putenv(buffer local) */

	/* UserPort declared in fpac.conf (list of AX.25 ports open to users,
	 * "*" meaning all of them) -- NOT to be confused with "Connected via"
	 * below, which is the port/address this particular session actually
	 * came in on. */
	userports[0] = '\0';
	for (pp = cfg.port ; pp != NULL ; pp = pp->next)
	{
		if (userports[0] != '\0')
			strncat(userports, " ", sizeof(userports) - strlen(userports) - 1);
		strncat(userports, pp->name, sizeof(userports) - strlen(userports) - 1);
	}
	if (userports[0] == '\0')
		strcpy(userports, "-");

	node_msg("Status:");
	time(&t);
	tprintf("System time      : %s", ctime(&t));
	if (uname(&name) == -1)
		tprintf("Cannot get system name\n");
	else	{
		if(Colored)	{
		tprintf("Hostname         : %s%s%s\n", NodeColors.node, name.nodename, ResetColor);
		tprintf("L3 callsign      : %s%s%s\n", NodeColors.indicatif,cfg.callsign,ResetColor);
		tprintf("L2 callsign      : %s%s%s\n",  NodeColors.indicatif,cfg.alt_callsign, ResetColor);
		tprintf("Traceroute call  : %s%s%s\n",  NodeColors.indicatif, cfg.trt_callsign, ResetColor);
		tprintf("DNIC, address    : %s%s,%s%s\n",  NodeColors.adresse,cfg.dnic, cfg.address, ResetColor);
			if (cfg.inetport != 0)
				tprintf("UDP/TCP/IP port  : %s%d%s\n", NodeColors.version,cfg.inetport, ResetColor);
		tprintf("Default port     : %s\n", cfg.def_port);
		tprintf("User port        : %s\n", userports);
		tprintf("Connected via    : %s\n", User.ul_name);
		tprintf("Inet address     : %s\n", cfg.def_addr);
		tprintf("City             : %s\n", cfg.city);
		tprintf("Zip - State      : %s\n", cfg.state);
		tprintf("Country          : %s\n", cfg.country);
		tprintf("Locator          : %s\n\n", cfg.locator);
		tprintf("Operating system : %s%s %s (%s)%s\n", NodeColors.adresse, name.sysname,
				name.release, name.machine,ResetColor);
		tprintf("FPAC version     : %s%s %s(built %s %s)%s\n", NodeColors.version, VERSION, NodeColors.node, __DATE__, __TIME__, ResetColor);
		}
		
		else {
		tprintf("Hostname         : %s\n", name.nodename);
		tprintf("L3 callsign      : %s\n", cfg.callsign);
		tprintf("L2 callsign      : %s\n", cfg.alt_callsign);
		tprintf("Traceroute call  : %s\n", cfg.trt_callsign);
		tprintf("DNIC, address    : %s,%s\n", cfg.dnic, cfg.address);
/*		tprintf("Inet Port        : %s\n", cfg.inetport);*/
			if (cfg.inetport != 0)
				tprintf("UDP/TCP/IP port  : %d\n", cfg.inetport);
		tprintf("Default port     : %s\n", cfg.def_port);
		tprintf("User port        : %s\n", userports);
		tprintf("Connected via    : %s\n", User.ul_name);
		tprintf("Inet address     : %s\n", cfg.def_addr);
		tprintf("City             : %s\n", cfg.city);
		tprintf("Zip - State      : %s\n", cfg.state);
		tprintf("Country          : %s\n", cfg.country);
		tprintf("Locator          : %s\n\n", cfg.locator);
		tprintf("Operating system : %s %s (%s)\n", name.sysname,
				name.release, name.machine);
		tprintf("FPAC version     : %s (built %s %s)\n",VERSION, __DATE__, __TIME__);
		}

/* read and calculate the amount of uptime and format it nicely */
	uptime(&uptime_secs, &idle_secs);
	updays = (int) uptime_secs / (60 * 60 * 24);
	upminutes = (int) uptime_secs / 60;
	uphours = upminutes / 60;
	uphours = uphours % 24;
	upminutes = upminutes % 60;
	tprintf("Uptime           : ");
	if (updays)
		tprintf("%d day%s, ", updays, (updays != 1) ? "s" : "");
	if (uphours)
		tprintf("%d hour%s ", uphours, (uphours != 1) ? "s" : "");
	tprintf("%d minute%s\n", upminutes, (upminutes != 1) ? "s" : "");
	loadavg(&av[0], &av[1], &av[2]);
	tprintf("Load average     : %.2f, %.2f, %.2f\n", av[0], av[1], av[2]);
	
	if (strlen((mem = meminfo())) == 0 )
		tprintf("Cannot get memory information !\n");
	else 
	{
		p = mem;
		for (i=0; i < MAX_ROW && *p; i++)	
		{
			value[i] = 0;
			l = sscanf(p, "%s%n", memo[i], &k);	
			p += k;
			memo[i][16] = '\0';			
			while(*p && !isdigit(*p)) p++;	
			{
				l = sscanf(p, "%u%n", &value[i], &k);
				p += k;					
			    if (*p == '\n' || l < 1)
					break;
			    l = sscanf(p, "%s%n", kb, &k);
			    p += k;
			}
	    	}	
		for (i = 0; i < MAX_ROW; i++)
		{
			if(!strcmp(memo[i], "MemTotal:"))
				memtotal = value[i];
			if(!strcmp(memo[i], "MemFree:"))
				memfree = value[i];	
			if(!strcmp(memo[i], "Buffers:"))
				buffers = value[i];
			if(!strcmp(memo[i], "Cached:"))
				cached = value[i];
			if(!strcmp(memo[i], "SwapTotal:"))
				swaptotal = value[i];
			if(!strcmp(memo[i], "SwapCached:"))
				swapcached = value[i];
			if(!strcmp(memo[i], "SwapFree:"))
				swapfree = value[i];
		}
			tprintf("Memory           : %7d Kb available ", memtotal);
			tprintf("%7d Kb used   ", (memtotal-memfree));
			tprintf("%7d Kb free\n", memfree);
			tprintf("Swap             : %7d Kb available ", swaptotal);
			tprintf("%7d Kb cached ", swapcached);
			tprintf("%7d Kb free\n\n", swapfree); 
		
	}	// else meminfo
	}	// got system name

	{
/*		struct proc_ax25 *p, *list;	--> *axp *axlist */

		if ((axlist = read_proc_ax25()) == NULL && errno != 0)
			node_perror("do_status: read_proc_ax25", errno);

		n = 0;
		for (axp = axlist; axp != NULL; axp = axp->next)
		{
			if (!strcmp(axp->dest_addr, "*"))
				continue;
			++n;
		}
		tprintf("L2 Users         : %d\n", n);
		free_proc_ax25(axlist);
	}

	{
/*		struct proc_rs *rp, *rlist;*/

		if ((rlist = read_proc_rs()) == NULL && errno != 0)
			node_perror("do_status: read_proc_rs", errno);

		n = 0;
		for (rp = rlist; rp != NULL; rp = rp->next)
		{
			if (!strcmp(rp->dest_addr, "*"))
				continue;

			if ((wp_check_call(rp->src_call) != 0)
				&& (wp_check_call(rp->dest_call) != 0))
				continue;

			/*if (rp->lci >= 2048)*/
			/*if (rp->lci > ROSE_DEFAULT_MAXVC)
				continue;*/

			++n;
		}
		tprintf("FPAC L3 Users    : %d\n", n);
		free_proc_rs(rlist);
	}

	{
/*		struct proc_rs_route *tp, *tlist;*/

		if ((tlist = read_proc_rs_routes()) == NULL && errno != 0)
			node_perror("do_status: read_proc_rs_routes:", errno);

		n = 0;
		for (tp = tlist; tp != NULL; tp = tp->next)
		{
			/* Do not display no-peers */
			if ((argc < 2)
				&& (!strcmp(tp->address1, "*") || !strcmp(tp->address2, "*")))
				continue;

			++n;
		}
		tprintf("FPAC L3 Transits : %d\n", n);
		free_proc_rs_routes(tlist);
	}

	{
/*		struct proc_rs_neigh *rp, *rlist; -->  *rsne *rsnelist*/

		if ((rsnelist = read_proc_rs_neigh()) == NULL && errno != 0)
			node_perror("do_status: read_proc_rs_neigh", errno);

		n = 0;
		for (rsne = rsnelist; rsne != NULL; rsne = rsne->next)
		{
			if (strncmp(rsne->call, "RSLOOP", 6) == 0)
			{
				loopback = rsne->addr;
				continue;
			}
			++n;
		}
		tprintf("FPAC adjacents   : %d\n", n);
		free_proc_rs_neigh(rsnelist);
	}

	{
/*		struct proc_rs_nodes *rp, *rlist;   --> *rsno  *rsnolist  */

		if ((rsnolist = read_proc_rs_nodes()) == NULL && errno != 0)
			node_perror("do_status: read_proc_rs_nodes", errno);

		n = 0;
		for (rsno = rsnolist; rsno != NULL; rsno = rsno->next)
		{
			if (rsno->neigh1 == (unsigned int)loopback)
				continue;
			++n;
		}
		tprintf("FPAC Routes      : %d\n", n);
		free_proc_rs_nodes(rsnolist);
	}

	n = wpcheck();

	return 0;
}

/* Lines per page of the WP command, then "<Return> more, A abort" */
#define WP_PAGE_LINES	20

int do_wp(int argc, char **argv)
{
	/* F6BVP 2026-09-30: all records (server limit 200), most recent
	 * first, shown page by page. It used to ask for 20 records only,
	 * sorted by address, which looked incomplete and unordered. */
	int nb = 200;
	unsigned int flags = WP_DATESORT_FLAG;
	int nbrec, lines = 0, stop = 0;
	char *answer;
//	unsigned int flags = 0;
	int p;
	int i, j;
	int ndigis;
	wp_t *wp;
	char *add;
	char *call;
	char dnic[5];
	char buf[20];

/*	if (argc < 2)
	{
		node_msg ("Usage: wp [-acdnrl nb] callsign");
		node_msg ("options :\n  -n = nodes only\n  -l nb max number of answers");       
		node_msg ("sort by :\n  -a address\n  -c callsign (default)\n  -d date\n  -r reverse");
		node_msg ("");
		return (1);
	}
*/
	optind = 0;

	while ((p = getopt(argc, argv, "acdl:nr")) != -1)
	{
		switch (p)
		{
		case 'c':
			flags &= ~(WP_ADDRSORT_FLAG | WP_DATESORT_FLAG);
			break;
		case 'l':
			nb = strtoul(optarg, NULL, 10);
			break;
		case 'n':
			flags |= WP_NODE_FLAG;
			break;
		case 'r':
			flags |= WP_REVERSE_FLAG;
			break;
		case 'a':
			flags &= ~(WP_DATESORT_FLAG);
			flags |= WP_ADDRSORT_FLAG;
			break;
		case 'd':
			flags &= ~(WP_ADDRSORT_FLAG);
			flags |= WP_DATESORT_FLAG;
			break;
		default :
			node_msg ("Usage: wp [-acdnrl nb] callsign");
			node_msg ("options :\n  -n = nodes only\n  -l nb max number of answers");       
			node_msg ("sort by :\n  -a address\n  -c callsign\n  -d date, most recent first (default)\n  -r reverse");
			node_msg ("");
			return(1);
			break;
		}
	}

	if (optind == argc)
		argv[optind] = "*";
		
	if (wp_open("NODE") == 0) {

	if (nb > 200)
	{
		node_msg("Your request has been limited to 200 results");
		nb = 200;
	}

//	tprintf("FPAC White Pages database : %d callsigns\n", wp_nb_records());

	wp = NULL;
	if (wp_get_list(&wp, &nb, flags, argv[optind]) == -1)
		nb = 0;
	/* Everything is read: close the WP connection before displaying, so
	 * that waiting at the page prompt keeps no connection to fpacwpd. */
	nbrec = wp_nb_records();
	wp_close();

	if (nb > 0)
	{
		for (i = 0; i < nb && !stop; i++)
		{
			if (wp[i].date == 0L)
				break;

			if (i == 0)
			{
				if (Colored)
					tprintf(" %sCallsign%s   %sLast update UTC%s   %sDNIC address%s  %sN/U\t Digi%s \t  %sLocator City%s\n", NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor, NodeColors.en_tete, ResetColor);
				else
					tprintf(" Callsign   Last update UTC   DNIC address  N/U\t Digi \t  Locator City\n");
			}

// Display nodes and users with one line per digi
			ndigis = wp[i].address.srose_ndigis;

			for (j = ndigis ; j >= 0; j--)
			{
			if ((ndigis != j ) || wp[i].is_node || (j==0 && ndigis == 0))
			{
			add = rose_ntoa(&wp[i].address.srose_addr);
			call = ax25_ntoa(&wp[i].address.srose_call);

			strncpy(dnic, add, 4);
			dnic[4] = '\0';

			my_date(buf, wp[i].date);
			if (Colored)
				tprintf("%s %-9s %s %s => %s %-7s ", NodeColors.indicatif, call, ResetColor, buf, dnic, add + 4);
			else
				tprintf("%-9s %s => %s %-7s ", call, buf, dnic, add + 4);

			if (wp[i].is_node)
				if (Colored)
					tprintf("%s Node %s", NodeColors.qualite_nulle, ResetColor);
				else
					tprintf(" Node ");
			else
				tprintf(" User ");

			if (wp[i].address.srose_ndigis == 0)
				tprintf("  -  ");
			else
			{
				call = ax25_ntoa(&wp[i].address.srose_digis[j]);
//				if (strstr(call,"-") == NULL)
//					strcat(call,"-0");
				if (Colored)
					tprintf("%s%-9s%s",NodeColors.indicatif, call, ResetColor);
				else
					tprintf("%-9s", call);
			}
			tprintf("\t  %s  %s\n", wp[i].locator, wp[i].city);

			if (++lines % WP_PAGE_LINES == 0 && (i + 1 < nb || j > 0))
			{
				tprintf("%s", T("-- <Return> more, A abort -- "));
				usflush(STDIN_FILENO);	/* tprintf() writes to STDIN_FILENO (io.c) */
				while ((answer = readline(User.fd)) == NULL)
				{
					if (errno == EINTR)
						continue;
					wp_free_list(&wp);
					logout("User disconnected");
				}
				if (toupper((unsigned char)*answer) == 'A')
				{
					stop = 1;
					break;
				}
			}
			}
			}
		}
		
	}

	if (nb == 0)
	{
		wp_free_list(&wp);
		node_msg("No WP matching \"%s\" !", argv[optind]);
		return (1);
	}

	tprintf("\n");
	

	tprintf("FPAC White Pages database : %d callsigns\n", nbrec);

	wp_free_list(&wp);
	}
	else {
		node_msg("Cannot open WP \n");
		return (1);
	}
	
	
	return (0);
}

/*
 * From azwnode
 */

int do_dest(int argc, char **argv)
{
	struct flex_dst *fdst, *p;
	struct flex_gt *flgt, *q;
	char ssid[12];
	int i = 0;
	int found = 0;

	if ((fdst = read_flex_dst()) == NULL)
	{
		if (errno)
			node_msg("flexd is probably not loaded");
		else
			node_msg("No FlexNet destinations");
		
		node_msg("Read /var/log/fpac.log file");
		return 0;
	}

	/* "dest" */
	if (argc == 1)
	{
		node_msg("FlexNet Destinations:");
		node_msg("Callsign ssid  rtt  Callsign ssid  rtt  Callsign ssid  rtt  Callsign ssid  rtt");
		for (p = fdst; p != NULL; p = p->next)
		{
			sprintf(ssid, "%d-%d", p->ssida, p->sside);
			tprintf("%-7s %-5s %4ld%s", p->dest_call, ssid, p->rtt,
					(++i % 4) ? "  " : "\n");
		}
		if ((i % 4) != 0)
			tprintf("\n");
		free_flex_dst(fdst);
		return 0;
	}

	if ((flgt = read_flex_gt()) == NULL)
	{
		node_perror("do_dest: read_flex_gt", errno);
		free_flex_dst(fdst);
		return 0;
	}

	if (strpbrk(argv[1], "*?&=#@"))
	{
		/* "dest *" */
		for (p = fdst; p != NULL; p = p->next)
		{
			if (!strmatch(p->dest_call, argv[1]))
				continue;
			if (!found)
			{
				node_msg("FlexNet Destinations:");
				tprintf("Dest     SSID    RTT Gateway\n");
				tprintf("-------- ----- ----- --------\n");
			}
			sprintf(ssid, "%d-%d", p->ssida, p->sside);
			q = find_gateway(p->addr, flgt);
			tprintf("%-8s %-5s %5ld %-8s\n", p->dest_call, ssid, p->rtt,
					q != NULL ? q->call : "?");
			found = 1;
		}
	}
	else if (strpbrk(argv[1], "-"))
	{
		/* "dest <call>" */
		p = find_dest(argv[1], fdst);
		if (p != NULL)
		{
			node_msg("FlexNet Destination %s:", p->dest_call);
			tprintf("Dest     SSID    RTT Gateway\n");
			tprintf("-------- ----- ----- --------\n");
			sprintf(ssid, "%d-%d", p->ssida, p->sside);
			q = find_gateway(p->addr, flgt);
			tprintf("%-8s %-5s %5ld %-8s\n", p->dest_call, ssid, p->rtt,
					q != NULL ? q->call : "?");
			found = 1;
		}
	}
	else
	{
		for (p = fdst; p != NULL; p = p->next)
		{
			if (!callmatch(p->dest_call, argv[1]))
				continue;
			if (!found)
			{
				node_msg("FlexNet Destinations:");
				tprintf("Dest     SSID    RTT Gateway\n");
				tprintf("-------- ----- ----- --------\n");
			}
			sprintf(ssid, "%d-%d", p->ssida, p->sside);
			q = find_gateway(p->addr, flgt);
			tprintf("%-8s %-5s %5ld %-8s\n", p->dest_call, ssid, p->rtt,
					q != NULL ? q->call : "?");
			found = 1;
		}
	}

	if (!found)
	{
		node_msg("No such destination");
	}

	free_flex_dst(fdst);
	free_flex_gt(flgt);

	return 0;
}
