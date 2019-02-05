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
#include "sysinfo.h"

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
	wp_t *wp;
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
			fread(&wph, sizeof(wph), 1, fptr);
			fclose(fptr);
		n = wph.nb_record;
		}
	}
	tprintf("FPAC White Pages : %d\n", n);
	free(wp);

	return (0);
}

void init_nodecmds(void)
{
	add_internal_cmd(&Nodecmds, "?", 1, 1, do_help);
	add_internal_cmd(&Nodecmds, "Alias", 1, 1, do_alias);
	add_internal_cmd(&Nodecmds, "APplications", 2, 1, do_application);
	add_internal_cmd(&Nodecmds, "Bye", 1, 1, do_bye);
	add_internal_cmd(&Nodecmds, "Connect", 1, 1, do_connect);
	add_internal_cmd(&Nodecmds, "Dest", 1, 1, do_dest);
	add_internal_cmd(&Nodecmds, "Finger", 1, 1, do_finger);
	add_internal_cmd(&Nodecmds, "Help", 1, 1, do_help);
	add_internal_cmd(&Nodecmds, "HOst", 2, 1, do_host);
	add_internal_cmd(&Nodecmds, "Info", 1, 1, do_help);
	add_internal_cmd(&Nodecmds, "Links", 1, 1, do_links);
	add_internal_cmd(&Nodecmds, "Mheard", 1, 1, do_mheard);
	add_internal_cmd(&Nodecmds, "NEtrom", 2, 1, do_netrom);
	add_internal_cmd(&Nodecmds, "Nodes", 1, 1, do_rose);
	add_internal_cmd(&Nodecmds, "PIng", 2, 1, do_ping);
	add_internal_cmd(&Nodecmds, "Ports", 1, 1, do_ports);
	add_internal_cmd(&Nodecmds, "Quit", 1, 1, do_bye);
	add_internal_cmd(&Nodecmds, "Routes", 1, 1, do_routes);
	add_internal_cmd(&Nodecmds, "Status", 1, 1, do_status);
	add_internal_cmd(&Nodecmds, "SYSop", 3, 1, do_sysop);
	add_internal_cmd(&Nodecmds, "Telnet", 1, 1, do_connect);
	add_internal_cmd(&Nodecmds, "Users", 1, 1, do_users);
	add_internal_cmd(&Nodecmds, "Wp", 1, 1, do_wp);

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
	logout("Bye");
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
				node_msg("Invalid port");
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
		tprintf
			("Callsign  Port    Pkts-rcvd I-Frames S-Frames U-Frames Time ago\n");
	}
	else if (port)
	{
		tprintf("Last %d Heard list for port %s :\n", nb, port);
		tprintf("Callsign  Port    Pkts-rcvd Mode Time ago\n");
	}
	else
	{
		tprintf("Last %d Heard list for all ports :\n", nb);
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
				tprintf("%-9s %-7.7s %-9ld %-8ld %-8ld %-8ld %s\n",
						t, list->data.portname, list->data.count,
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

				tprintf("%-9s %-7.7s %-9ld %-4s %s\n",
						t, list->data.portname, list->data.count, s,
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
	char fname[80], line[256];
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
	strcpy(fname, FPAC_HELP_DIR);
	if (*argv[0] == 'i')
	{							/* "info"   */
		strcpy(fname, FPAC_INFO_FILE);
		node_msg("%s v %s (built %s) for LINUX\n", "FPAC-Node", VERSION,
				 __DATE__);
	}
	else if (argc == 1)
	{							/* "help"   */
		strcat(fname, "Help.hlp");
	}
	else
	{							/* "help <cmd>" */
/*int */	found = 0;
/*		DIR *dir;
		struct dirent *ent;
*/
		dir = opendir(FPAC_HELP_DIR);
		if (dir)
		{
			/* Search in the help directory the best matching name */
			for (;;)
			{
/*				int len;
				char *ptr;
*/
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

				if ((len == 0) || (len < strlen(argv[1])))
					len = strlen(argv[1]);

				if (strncasecmp(ent->d_name, argv[1], len) == 0)
				{
					strcat(fname, ent->d_name);
					found = 1;
					break;
				}
			}

			if (!found)
			{
				strcat(fname, "not_found.hlp");
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

int do_ports(int argc, char **argv)
{
	char *cp = NULL;
	int n;

	if (rs_config_get_next(cp) == NULL)
		n = rs_config_load_ports();

	if ((argc > 1) && (*argv[1] == '?'))
	{
		node_msg("usage : ports");
		return (0);
	}
	node_msg("Ports:\nPort\tDev\t Description");
	while ((cp = ax25_config_get_next(cp)) != NULL)
	{
		tprintf("%-6s\t%-6s\t %s\n", cp, ax25_config_get_dev(cp), ax25_config_get_desc(cp));
	}
	while ((cp = rs_config_get_next(cp)) != NULL)
	{
		tprintf("%-6s\t%-6s\t%s\n", cp, rs_config_get_dev(cp), rs_config_get_desc(cp));
	}
	while ((cp = nr_config_get_next(cp)) != NULL)
	{
		tprintf("%-6s\t%-6s\t %s\n", cp, nr_config_get_dev(cp), nr_config_get_desc(cp));
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
	int len;
/*	char neigh[20],	char nei1[20], nei2[20];*/

	if ((argc > 1) && (*argv[1] == '?'))
	{
		node_msg("usage : users [callsign]");
		return (0);
	}

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
			tprintf("Port   Callsign     Callsign  AX.25 state  ROSE state  NetRom status");
			if (is_sysop())
				tprintf(" Unack   T1      T3      Retr  Rtt Snd-Q Rcv-Q");
			tprintf("\n");
		}

		len = strlen(p->src_addr);
		if ((len > 0) && (p->src_addr[len - 1] == '*'))
			p->src_addr[len - 1] = '\0';

		cp = ax25_config_get_name(p->dev);
		if (cp != NULL) {
/*		if (cp == NULL)
			cp = "All";*/

		tprintf("%-6s %-9s -> %-9s ", cp, p->src_addr, p->dest_addr);
		if (!strcmp(p->dest_addr, "*"))
		{
			tprintf("Listening\n");
			continue;
		}
		switch (p->st)
		{
		case 0:
			cp = "Disconnected";
			break;
		case 1:
			cp = "Conn pending";
			break;
		case 2:
			cp = "Disc pending";
			break;
		case 3:
			cp = "Connected   ";
			break;
		case 4:
			cp = "Recovery    ";
			break;
		default:
			cp = "Unknown     ";
			break;
		}
		tprintf("%s", cp);
		
		tprintf((node_is_connected(p->dest_addr) ? " Connected" : " ---------"));
		tprintf((netrom_node_is_connected(p->dest_addr) ? "   Connected" : "   ---------"));

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
		}
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
			&& strcasecmp(rp->dest_call, argv[1])
			&& strcasecmp(rp->src_call, argv[1]))
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
			tprintf
				("Callsign  DNIC addr   <-> Callsign  DNIC addr   LCI Adjacent    AX.25 state");
			if (is_sysop())
				tprintf("    Unack Snd-Q Rcv-Q");
			tprintf("\n");
		}

		*neigh = '\0';
		for (pv = listv; pv != NULL; pv = pv->next)
			if (rp->neigh == pv->addr)
				sprintf(neigh, "(%s)", pv->call);

		tprintf("%-9s %-10s ", rp->src_call, roseaddr(rp->src_addr));
		tprintf("<-> %-9s %-10s %03X %-11s ",
				rp->dest_call, roseaddr(rp->dest_addr), rp->lci, neigh);
		if (!strcmp(rp->dest_addr, "*"))
		{
			tprintf("Listening\n");
			continue;
		}
		switch (rp->st)
		{
		case 0:
			cp = "Disconnected";
			break;
		case 1:
			cp = "Conn pending";
			break;
		case 2:
			cp = "Disc pending";
			break;
		case 3:
			cp = "Connected   ";
			break;
		case 4:
			cp = "Recovery    ";
			break;
		default:
			cp = "Unknown     ";
			break;
		}
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
			&& strcasecmp(tp->call1, argv[1])
			&& strcasecmp(tp->call2, argv[1]))
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
			tprintf
				("Callsign  DNIC addr   LCI Adjacent   <-> Callsign  DNIC addr   LCI Adjacent\n");
		}

		*nei1 = '\0';
		for (pv = listv; pv != NULL; pv = pv->next)
			if (tp->neigh1 == pv->addr)
				sprintf(nei1, "(%s)", pv->call);
		tprintf("%-9s %-10s %03X %-11s",
				tp->call1, roseaddr(tp->address1), tp->lci1, nei1);

		*nei2 = '\0';
		for (pv = listv; pv != NULL; pv = pv->next)
			if (tp->neigh2 == pv->addr)
				sprintf(nei2, "(%s)", pv->call);
		tprintf("<-> %-9s %-10s %03X %s\n",
				tp->call2, roseaddr(tp->address2), tp->lci2, nei2);

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
		node_msg("routes : address missing");
		return 0;
	}

	nodeaddr[10] = '\0';
	memset(&nodeaddr, '0', 10);
	len = strlen(argv[2]);
	if ((len < 4) || (len > 10) || (strspn(argv[2], "0123456789") != len))
	{
		node_msg("routes : address error (4 to 10 digits)");
		return 0;
	}

	strncpy(nodeaddr, argv[2], len);

	if (argc < 4)
	{
		node_msg("routes : adjacent callsign missing");
		return 0;
	}

	rs_node.mask = len;

	if (rose_aton(nodeaddr, rs_node.address.rose_addr) != 0)
	{
		node_msg("do_manage_routes: invalid address %s", nodeaddr);
		return (0);
	}

	/* Search device for the adjacent */
	if ((listv = read_proc_rs_neigh()) == NULL)
	{
		node_msg("do_manage_routes: error read_proc_nr_neigh");
		return 0;
	}

	for (pv = listv; pv != NULL; pv = pv->next)
		if (strcasecmp(argv[3], pv->call) == 0)
			break;

	free_proc_rs_neigh(listv);

	if (pv == NULL)
	{
		node_msg("adjacent %s not found", argv[3]);
		return 0;
	}

	strcpy(rs_node.device, pv->dev);

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
	int first_node = 1;
	int loopback = -1;
	char stradd[11];
	char *addr = NULL;
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
			|| (strspn(argv[1], "0123456789") != len))
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
		tprintf("Coverage\n");
		for (i = 0, cl = cfg.cover; cl; cl = cl->next)
		{
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
		node_msg("Address      Port    Description");
		for (al = cfg.addrp; al; al = al->next)
		{
			tprintf("%s,%s  %-6s  %s\n",
					cfg.dnic,
					al->addr,
					(al->port[0]) ? al->port : "?",
					(al->port[0]) ? ax25_config_get_desc(al->port) : "None");
		}
		tprintf("\n");
	}

	/* Routes */
	if ((listv = read_proc_rs_neigh()) == NULL)
	{
		if (errno)
			node_perror("do_routes: read_proc_rs_neigh", errno);
		return 0;
	}

	/* Search the node number of the loopback */
	for (pv = listv; pv != NULL; pv = pv->next)
		if (strncmp(pv->call, "RSLOOP", 6) == 0)
		{
			loopback = pv->addr;
			break;
		}

	if ((listn = read_proc_rs_nodes()) == NULL)
	{
		if (errno)
			node_perror("do_routes: read_proc_rs_nodes", errno);
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
		if (pn->neigh1 == loopback)
			continue;

		if ((addr) && (strncmp(addr, pn->address, pn->mask) != 0))
			continue;

		if (pn->address[0] == '*')
			continue;

		if (first)
		{
			node_msg("ROSE routes :\nDNIC Address Primary   Route  | 1st Alt   Route  | 2nd Alt   Route  |");
			first = 0;
		}

		if (pn->mask != 10) {

/*		if ((pn->mask == 10) &&  (first_node))
		{
			node_msg("Adjacent ROSE nodes routes :\nDNIC Address           Route");
			first_node = 0;
		}
*/
		for (i = pn->mask; i < 10; i++)
			pn->address[i] = '.';

		tprintf("%s ", roseaddr(pn->address));
		for (pv = listv; pv != NULL; pv = pv->next)
		{
			if (pn->neigh1 == pv->addr)
			{	
				tprintf(" %-9s", pv->call);
				if (!strcmp("yes",pv->restart))
					tprintf(" Opened |");
				else	tprintf(" Closed |");
			}
		}
		for (pv = listv; pv != NULL; pv = pv->next)
		{
			if (pn->neigh2 == pv->addr)
			{
				tprintf(" %-9s", pv->call);
				if (!strcmp("yes",pv->restart))
					tprintf(" Opened |");
				else	tprintf(" Closed |");
			}
		}
		for (pv = listv; pv != NULL; pv = pv->next)
		{
			if (pn->neigh3 == pv->addr)
			{
				tprintf(" %-9s", pv->call);
				if (!strcmp("yes",pv->restart))
					tprintf(" Opened |");
				else	tprintf(" Closed |");
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
		if (strcasecmp(argv[3], pv->call) == 0)
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
	struct proc_rs_neigh *np, *nlist;
	struct proc_nr *nr, *nrlist;
	struct proc_ax25 *p, *list;
	char *cp = NULL;
	char *pdev= NULL;
	int n;

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
		n = rs_config_load_ports();
	list = read_proc_ax25();

	node_msg("Adjacent Nodes Links:\nCallsign  Status       Dev    Iface  Port");

	/* "nodes" */
	for (p = list; p != NULL; p = p->next)
	{
		if (wp_check_call(p->dest_addr) !=0)
			continue;

		switch (p->st)
		{
		case 0:
			cp = "Disconnected";
			break;
		case 1:
			cp = "Conn pending";
			break;
		case 2:
			cp = "Disc pending";
			break;
		case 3:
			cp = "Connected   ";
			break;
		case 4:
			cp = "Recovery    ";
			break;
		default:
			cp = "Unknown     ";
			break;
		}
		if (pdev = ax25_config_get_name(p->dev)) {

		if (netrom_node_is_connected(p->dest_addr) && (p->st > 0))
			tprintf("%-9s %-12s %-6s %-6s %-6s\n",
					p->dest_addr,
					cp,
					nr_config_get_dev(nr_config_get_name(p->dest_addr)),
					p->dev,
					ax25_config_get_name(p->dev));
		if (node_is_connected(p->dest_addr))
			tprintf("%-9s %-12s %-6s %-6s %-6s\n",
					p->dest_addr,
					cp,
					rs_config_get_dev(rs_config_get_name(p->dest_addr)),
					p->dev,
					ax25_config_get_name(p->dev));

		}
		}

	free_proc_ax25(list);
	free_proc_rs_neigh(nlist);
	free_proc_nr(nrlist);

	return 0;
}

struct proc_nr_nodes *sort_proc_nr_nodes(struct proc_nr_nodes *list, int alphabetic_order) 
{
	struct proc_nr_nodes *p, *prec;

	if (list)
	{
		prec = NULL;
		/* Sort the list by decreasing quality or alphabetic callsign order */
		for (p = list; p->next;)
		{
			if (alphabetic_order)
			{
				if (strcmp(p->next->call,  p->call) <= 0)
				{	
				/* swap current and next */
				if (prec)
				{
					prec->next = p->next;
					p->next = prec->next->next;
					prec->next->next = p;
				}
				else
				{
					list = p->next;
					p->next = list->next;
					list->next = p;
				}
				p = list;
				prec = NULL;
				continue;
				}
			}
			else
			{
				if (p->next->qual1 > p->qual1)
				{
				/* swap current and next */
				if (prec)
				{
					prec->next = p->next;
					p->next = prec->next->next;
					prec->next->next = p;
				}
				else
				{
					list = p->next;
					p->next = list->next;
					list->next = p;
				}
				p = list;
				prec = NULL;
				continue;
				}
			}
			prec = p;
			p = p->next;
		}
	}
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
		tprintf ("Callsign  DNIC addr    Callsign  DNIC addr    Callsign  DNIC addr\n");
		if (wp_get_list(&wp, &nb, WP_NODE_FLAG, "*") != -1)
		{
			for (i = 0; i < nb; i++)
			{
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

				tprintf("%9.9s  %s  %3s  %s  %s\n",
					call,
					addr,
					(country) ? country : "", wp[i].locator, wp[i].city);
				ret = 0;
			}
		}


		wp_free_list(&wp);
	}

	wp_close();

	return ret;
}

int do_netrom(int argc, char **argv)
{
	struct proc_nr_nodes *p, *list;
	struct proc_nr_neigh *np, *nlist;
	int i = 0;
	int a = 0;
	int first;
	int fpac;

	if ((argc > 2) && (*argv[1] == '?') && (strlen(argv[1]) == 1))
	{
		node_msg("usage : nodes [alias|callsign]");
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
	/* F6BVP alphabetic call order option */
	if ((argc == 2) && (*argv[1] == 'a'))
	{
		argc--;
		a = 1;
	}
	list = sort_proc_nr_nodes(list,a);

	/* "nodes" */
	if (argc == 1)
	{
		if (fpac)
			tprintf("\n");
		node_msg("NetRom Nodes:");
		node_msg(" Alias:Callsign Qual.   Alias:Callsign Qual.   Alias:Callsign Qual.");
		for (p = list; p != NULL; p = p->next)
		{
			tprintf("%-16.16s(%-3d) %c",
					print_node(p->alias, p->call), p->qual1,
					((i + 1) % 3) ? ' ' : '\n');
			++i;
		}
		if ((i % 3) != 0)
			tprintf("\n");
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
		tprintf("Node              Quality Obsolescence Port   Neighbour\n");
		for (p = list; p != NULL; p = p->next)
		{
			tprintf("%-16.16s  ", print_node(p->alias, p->call));
		
			if (p->n == 0)		/* local node */
				tprintf("%-7d %-12d\n",
						p->qual1, p->obs1);
			else	
			if ((np = find_neigh(p->addr1, nlist)) != NULL)
			{
				tprintf("%-7d %-12d %-6s %s\n",
						p->qual1, p->obs1,
						ax25_config_get_name(np->dev), np->call);
			}
			if (p->n > 1 && (np = find_neigh(p->addr2, nlist)) != NULL)
			{
				tprintf("                  ");
				tprintf("%-7d %-12d %-6s %s\n",
						p->qual2, p->obs2,
						ax25_config_get_name(np->dev), np->call);
			}
			if (p->n > 2 && (np = find_neigh(p->addr3, nlist)) != NULL)
			{
				tprintf("                  ");
				tprintf("%-7d %-12d %-6s %s\n",
						p->qual3, p->obs3,
						ax25_config_get_name(np->dev), np->call);
			}
		}
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
			if (p->n > 1 && (np = find_neigh(p->addr2, nlist)) != NULL)
			{
				tprintf("%c     %-7d %-12d %-6s %s\n",
						p->w == 2 ? '>' : ' ',
						p->qual2, p->obs2,
						ax25_config_get_name(np->dev), np->call);
			}
			if (p->n > 1 && (np = find_neigh(p->addr3, nlist)) != NULL)
			{
				tprintf("%c     %-7d %-12d %-6s %s\n",
						p->w == 3 ? '>' : ' ',
						p->qual3, p->obs3,
						ax25_config_get_name(np->dev), np->call);
			}
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
	int i, j, k, l;
	struct proc_ax25 *axp, *axlist;
	struct proc_rs_route *tp, *tlist;
	struct proc_rs *rp, *rlist;
	struct proc_rs_nodes *rsno, *rsnolist;
	struct proc_rs_neigh *rsne, *rsnelist;
	cfg_t cfg;

	memtotal = memfree = buffers = cached = swaptotal = swapcached = swapfree = 0;

	if ((argc > 1) && (*argv[1] == '?'))
	{
		node_msg("usage : status");
		return (0);
	}

	if (cfg_open(&cfg) != 0)
	{
		return (0);
	}

	node_msg("Status:");
	time(&t);
	tprintf("System time      : %s", ctime(&t));
	if (uname(&name) == -1)
		tprintf("Cannot get system name\n");
	else
	{
		tprintf("Hostname         : %s\n", name.nodename);
		tprintf("L3 callsign      : %s\n", cfg.callsign);
		tprintf("L2 callsign      : %s\n", cfg.alt_callsign);
		tprintf("Traceroute call  : %s\n", cfg.trt_callsign);
		tprintf("DNIC, address    : %s,%s\n", cfg.dnic, cfg.address);
/*		tprintf("Inet Port        : %s\n", cfg.inetport);*/
	if (cfg.inetport != 0)
	{
		tprintf("UDP/TCP/IP port  : %d\n", cfg.inetport);
	}
		tprintf("Inet address     : %s\n", cfg.def_addr);
		tprintf("City             : %s\n", cfg.city);
		tprintf("Zip - State      : %s\n", cfg.state);
		tprintf("Country          : %s\n", cfg.country);
		tprintf("Locator          : %s\n\n", cfg.locator);
		tprintf("Operating system : %s %s (%s)\n", name.sysname,
				name.release, name.machine);
	}
	tprintf("FPAC version     : %s (built %s)\n",VERSION, __DATE__);
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

	{
	if (strlen((mem = meminfo())) == 0 )
		tprintf("Cannot get memory information !\n");
	else 
	{
		p = mem;				
		j = 0;
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
		
	}
	}

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
			if (rsno->neigh1 == loopback)
				continue;
			++n;
		}
		tprintf("FPAC Routes      : %d\n", n);
		free_proc_rs_nodes(rsnolist);
	}

	n = wpcheck();

	return 0;
}

int do_wp(int argc, char **argv)
{
	int nb = 20;
	unsigned int flags = 0;
	int p;
	int i, j;
	wp_t *wp;
	char *add;
	char *call;
	char dnic[5];
	char buf[20];

	/*if (argc < 2)
	   {
	   node_msg ("Usage: wp [-acdnrl nb] callsign");
	   node_msg ("options :\n  n = nodes only\n  l = max number of answers");       
	   node_msg ("sort by :\n  a address\n  c callsign (default)\n  d date\n  r reverse");
	   return (1);
	   } */

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
		}
	}

	if (optind == argc)
		argv[optind] = "*";

	if (wp_open("NODE") == 0) {

	tprintf("FPAC White Pages database : %d callsigns\n", wp_nb_records());

	if (nb > 1000)
	{
		node_msg("Your request has been limited to 1000 results");
		nb = 1000;
	}

	if (wp_get_list(&wp, &nb, flags, argv[optind]) != -1)
	{
		for (i = 0; i < nb; i++)
		{
			if (wp[i].date == 0L)
				break;

			if (nb == 0)
				tprintf("Callsign  Last update       DNIC address digis\n");

			add = rose_ntoa(&wp[i].address.srose_addr);
			call = ax25_ntoa(&wp[i].address.srose_call);

			strncpy(dnic, add, 4);
			dnic[4] = '\0';

			my_date(buf, wp[i].date);
			tprintf("%-9s %s => %s %-7s ", call, buf, dnic, add + 4);

			if (wp[i].is_node)
				tprintf("Node ");

			for (j = wp[i].address.srose_ndigis - 1; j >= 0; j--)
			{
				tprintf("%s ", ax25_ntoa(&wp[i].address.srose_digis[j]));
			}

			tprintf("%s %s\n", wp[i].locator, wp[i].city);
		}
	}

	if (nb == 0)
	{
		node_msg("No WP matching \"%s\" !", argv[optind]);
		return (1);
	}

	tprintf("\n");

	wp_free_list(&wp);
	wp_close();
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
