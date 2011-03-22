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
#include "../lib/wp.h"

#include "../lib/ax25compat.h"

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
	node_msg("\nroute %d - n->call %s mask %d node[nroute] %s\n", nroute, n->call, mask, f->node[nroute] );
				if (strcasecmp(f->node[nroute], n->call) == 0)
				{
	node_msg("dest %s ", f->node[nroute]);
		/* Only return address of a connected neighbour */ 
				if (node_is_connected(n->call))
					{		
	node_msg(" CONNECTED \n");						
					strcpy(retaddr, n->dnic);
					strcat(retaddr, n->addr);
					return retaddr;
					}
	node_msg("NOT CONNECTED \n");				
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
 */
static int connect_to(char *address[], int family, int escape, char *source)
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
		
	
		if (strcasecmp(address[0], cfg.alt_callsign) == 0)
		{
			node_msg("already connected to %s", cfg.alt_callsign);
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

		/* Default DNIC */
		memcpy(path, rs_get_addr(NULL), 4);

		addrlen = strlen(address[pos]);
		if (addrlen == 3)
		{
			/* Country designator */
			memcpy(path, des2dnic(address[pos]), 4);
			++pos;
			addrlen = strlen(address[pos]);
		}
		else if (addrlen == 4)
		{
			/* DNIC */
			memcpy(path, address[pos], 4);
			++pos;
			addrlen = strlen(address[pos]);
		}

		if ((addrlen != 6) && (addrlen != 10))
		{
			node_msg("Invalid ROSE address");
			return (-1);
		}

		memcpy(path + (10 - addrlen), address[pos], addrlen);

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
		paclen = rs_config_get_paclen(NULL); 
		eol = ROSE_EOL;
		break;

	case AF_NETROM:
		if (strcasecmp(address[0], cfg.alt_callsign) == 0)
		{
			node_msg("already connected to %s", cfg.alt_callsign);
			return -1;
		}

		if ((fd = socket(AF_NETROM, SOCK_SEQPACKET, 0)) < 0)
		{
			node_perror("connect_to: socket", errno);
			return -1;
		}
		/* Why on earth is this different from ax.25 ????? */
		sprintf(path, "%s %s", nr_config_get_addr(NrPort), call);
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
			node_msg("No such node");
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
                    node_msg("Invalid port");
                    return -1;
                }
/* DEBUG F6BVP */
		fprintf (stderr, "Family = AF_AX25 =%d address[0] ='%s' address[1] ='%s' call ='%s' dest ='%s' cfg.alt_callsign ='%s'\n", family, address[0], address[1], call, dest, cfg.alt_callsign);
		
		if (strcasecmp(address[0], cfg.alt_callsign) == 0)
		{
			node_msg("already connected to %s", call);
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

		strcpy(User.dl_name, strupr(address[1]));
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
			node_msg("Unknown host %s", address[0]);
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
			node_msg("Unknown service %s", address[1]);
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
		node_msg("Unsupported address family");
		return -1;
	}
	if (family == AF_FLEXNET)
		node_msg("Trying %s%s...", (source) ? source : "", User.dl_name);
	else
		node_msg("Trying %s%s... Type <RETURN> to abort", (source) ? source : "", User.dl_name);
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
			node_msg("*** Failure with %s", User.dl_name);
			node_msg("*** %s", reason(rose_cause.cause));
		}
		else
		{
			node_msg("*** Failure with %s", User.dl_name);
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
					node_msg("*** Failure with %s", User.dl_name);
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
					node_msg("*** Failure with %s%s", User.dl_name, origin);
					node_msg("*** %s", cp);
					free(cp);

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
				node_msg("*** Aborted");
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
			node_msg("link setup...");
			}
		else
			{
			if (escape == -1)
				node_msg("*** Connected to %s", User.dl_name);
			else
				node_msg("*** Connected to %s (Escape: ~. )", User.dl_name);
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
		if (strcasecmp(callsign, a->alias) == 0)
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
	char *ptr = NULL;
	int ret = 0;

	if ((list = read_proc_nr_nodes()) == NULL)
		return ret;

	for (p = list; p != NULL; p = p->next)
	{
		if ((strcasecmp(p->call, call) == 0)
			|| (strcasecmp(p->alias, call) == 0))
		{
			ptr = p->call;
			break;
		}
	}

	if (ptr)
	{
		strcpy(netrom_call, ptr);
		ret = 1;
	}

	free_proc_nr_nodes(list);

	return ret;
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

	source = NULL;

	/* Delete the "v" or "via" */
	for (cpt = 0, n = 0; n < argc; n++)
	{
		argv[cpt] = argv[n];
		if (strcasecmp(argv[n], "v") && strcasecmp(argv[n], "via"))
			++cpt;
	}
	argv[cpt] = NULL;
	argc = cpt;

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
		return 0;
	}

	/* Telnet connection */
	if (*argv[0] == 't')
	{
		family = AF_INET;
	}

	/* Check FPAC Aliases */
	else
	{
		wp_open("NODE");

		if (is_alias(argv[1], &alias))
		{
			argc = parse_args(argv + 1, alias.path) + 1;
		}

		/* Check if its is a known port */
		if (ax25_config_get_addr(argv[1]))
		{
			family = AF_AX25;
			source = "(user port) ";
		}

		/* Check if known NetRom node */
		else if ((argc == 2) && (is_netrom(argv[1], netromcall)))
		{
			argv[1] = netromcall;
			family = AF_NETROM;
			source = "(netrom node) ";
		}

		/* Check if in FPAC WP */
		else if ((argc == 2) && (is_wp(argv[1], &wpaddr)))
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
		else if (argc > 2)
		{
			if ((strlen(argv[2]) == 3) && (des2dnic(argv[2]) != NULL))
			{
				/* Digi is a country designator */
/* F6BVP			strcpy(argv[2], des2dnic(argv[2]));*/
				family = AF_ROSE;
			}
			else if (strspn(argv[2], "0123456789") == strlen(argv[2]))
			{
				/* Digi is a DNIC */
				family = AF_ROSE;
			}
			else
			{
				if (ax25_aton_entry(argv[2], ax25.ax25_call) == -1)
				{
					node_msg("invalid AX.25 port callsign - %s", argv[2]);
					wp_close();
					return (0);
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
		else if ((argc == 2) && ((flx = find_dest(argv[1], NULL)) != NULL))
		{						/* Check FlexNet */
			k = 1;
			strcpy(netromcall, argv[1]);
			flgt = find_gateway(flx->addr, NULL);
			if (flgt == NULL)
			{
				node_msg ("Error: No gateway for destination %s", netromcall);
				return 0;
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
		/* Try mheard */
		else if (is_heard(argv + 1))
		{
			family = AF_AX25;
			source = "(heard) ";
		}
		else
		{
			node_msg("Port missing : Connect port callsign");
			wp_close();
			return (0);
		}
		wp_close();
	}

	if (family == AF_INET && argc > 3)
		connstr = argv[3];
	/* escape = (check_perms(PERM_NOESC, 0L) == 0) ? -1 : EscChar; */

	escape = 1;

	if ((fd = connect_to(++argv, family, escape, source)) == -1)
	{
		set_eolmode(User.fd, EOLMODE_TEXT);
		if (fcntl(User.fd, F_SETFL, 0) == -1)
			node_perror("do_connect: fcntl - stdin", errno);
		return 0;
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
			node_msg("*** Disconnected%s", origin);
		node_msg("*** %02X%02X - %s", rose_cause.cause,
				 rose_cause.diagnostic, reason(rose_cause.cause));

	}

	close(fd);
	fpaclog(LOGLVL_GW, "Disconnected from %s%s", User.dl_name, origin);
	if (stay)
	{
		set_eolmode(User.fd, EOLMODE_TEXT);
		if (fcntl(User.fd, F_SETFL, 0) == -1)
			node_perror("do_connect: fcntl - stdin", errno);
		node_msg("*** Reconnected to %s", NodeId);
	}
	else
		logout("No reconnect");
	return 0;
}

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
	if ((fd = connect_to(addr, AF_INET, -1, "(finger) ")) != -1)
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
		node_msg("*** Reconnected to %s", NodeId);
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
		node_msg("Unknown host %s", argv[1]);
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
				node_msg("Aborted");
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
