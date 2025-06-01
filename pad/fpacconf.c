
/******************************************************
 * fpacconf.c                                         *
 * FPAC project.            FPAC PAD                  *
 *                                                    *
 * Parts of code from different sources of ax25-utils *
 *                                                    *
 * F6FBB 05-1997                                      *
 *                                                    *
 ******************************************************/

/******************************************************
 * 12/05/97 1.00 F6FBB First draft !
 *
 ******************************************************/
 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <ctype.h>
/*#include <netdb.h>*/
#include <time.h>
#include <sys/types.h>
/*#include <sys/fcntl.h>*/
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <net/if_arp.h>
#include <net/if.h>

#include "../pathnames.h"
#include "ax25compat.h"
#include "fpad.h"

static int l3_conf(int verbose);
static void create_rsports(void);

int read_conf(int verbose)
{
	if (cfg_open(&cfg) != 0)
		return(0);

	if (l3_conf(verbose) == 0)
		return(0);

	return(1);
}

/* Check if the last modified of the conf file has changed */
int conf_changed(void)
{
	struct stat st;

	if (stat(FPACCONF, &st) == 0)
	{
		if (cfg.date < st.st_mtime)
			read_conf(1);
	}
	if (stat(FPACNODES, &st) == 0)
	{
		if (cfg.date < st.st_mtime)
			read_conf(1);
	}
	if (stat(FPACROUTES, &st) == 0)
	{
		if (cfg.date < st.st_mtime)
			read_conf(1);
	}
	return(0);
}

static int add_route(int s, route_t *r)
{
	int i;
	char *dev;
	char *ptr;
	char nodeaddr[11];
	char node_addr[10];
	char nodes[80];
	char call[80];
	node_t *n = NULL;
	struct rose_route_struct rs_node;

	nodeaddr[10] = '\0';
	memset(&node_addr, '0', 10);

	rs_node.mask = strlen(r->addr);

	strcpy(nodeaddr, r->addr);
	strncat(nodeaddr, node_addr, 10 - rs_node.mask);
	
	if (rose_aton(nodeaddr, rs_node.address.rose_addr) != 0) 
	{
		fprintf(stderr, "rsparms: nodes: invalid address %s\n", nodeaddr);
		return(0);
	}

	/* Validates all nodes for this route */

	strcpy(nodes, r->nodes);
	ptr = strtok(nodes, " ");

	while (ptr)
	{
		/* Search node in the node list */
		n = cfg.node;
		while (n)
		{
			if (strcasecmp(n->name, ptr) == 0)
				break;
			n = n->next;
		}

		if (n == NULL)
		{
			fprintf(stderr, "FPAD : node %s not found\n", ptr);
			return(0);
		}
		printf("Route %s -> %s (%s)\n", nodeaddr, n->call, n->port);
		
		if ((dev = ax25_config_get_dev((n->port[0]) ? n->port : NULL)) == NULL) 
		{
			fprintf(stderr, "FPAD : invalid port name {%s}\n", n->port);
			return(0);
		}

		strcpy(rs_node.device, dev);

		strcpy(call, n->call);
		ptr = strtok(call, " ");

		if (ptr)
		{
			if (ax25_aton_entry(ptr, rs_node.neighbour.ax25_call) != 0) 
			{
				fprintf(stderr, "FPAD : add_route() invalid neighbour callsign %s\n", ptr);
				return(0);
			}
		}

		for (i = 0; (ptr = strtok(NULL, " ")) && i < AX25_MAX_DIGIS; i++) 
		{
			if (ax25_aton_entry(ptr, rs_node.digipeaters[i].ax25_call) != 0) 
			{
				fprintf(stderr, "FPAD : add_route() invalid digi callsign %s\n", ptr);
				return(0);
			}
		}

		rs_node.ndigis = i;

		if (ioctl(s, SIOCADDRT, &rs_node) == -1)
		{
			perror("FPAD : add_route() SIOCADDRT");
			return(0);
		}

		ptr = strtok(NULL, " ");
	}

	return(1);
}

static int l3_conf(int verbose)
{
	int s;
	ax25_address rose_call;
	route_t *r = NULL;

	/* Configure the Node Callsign */
	
	if (ax25_aton_entry(cfg.callsign, rose_call.ax25_call) == -1) 
	{
		fprintf(stderr, "FPAD : invalid callsign %s\n", cfg.callsign);
		return(0);
	}

	if ((s = socket(AF_ROSE, SOCK_SEQPACKET, 0)) < 0) 
	{
		perror("FPAD l3_conf: socket");
		return(0);
	}
		
	if (ioctl(s, SIOCRSCLRRT, NULL) < 0 )
	{
		perror("FPAD l3_conf: SIOCRSCLRRT");
		/* close(s);
		return(0); */
	}

	if (ioctl(s, SIOCRSSL2CALL, &rose_call) < 0) 
	{
		perror("FPAD l3_conf: SIOCRSSL2CALL");
		close(s);
		return(0);
	}

	printf("Configuring routes :\n");
	
	/* Add routes to adjacents */
	r = cfg.route;

	/* DEBUG F6BVP */
	if ((r->nodes == NULL) || (r->addr == NULL)) 
			fprintf(stderr, "FPAD WARNING : no node or routed address configured !\n");
	else
	while (r)
	{
		if (add_route(s, r) == 0)
		{
			fprintf(stderr, "FPAD : error in route %s %s\n", r->addr, r->nodes);
			close(s);
			return(0);
		}
		r = r->next;
	}

	printf("\n");

	close(s);

	create_rsports();
	
	return(1);
}

static int getfreedev(char *dev, char *address)
{
	struct ifreq ifr;
	int fd;
	int i;
	
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("FPAD : rsattach socket");
		return FALSE;
	}

	for (i = 0; i < 10; i++) 
	{
		sprintf(dev, "rose%d", i);
		strcpy(ifr.ifr_name, dev);
	
		if (ioctl(fd, SIOCGIFHWADDR, &ifr) != 0) 
		{
			perror("FPAD : rsattach SIOCGIFHWADDR");
			close(fd);
			return(-1);
		}

		if (strcmp(address, rose_ntoa((rose_address *)ifr.ifr_hwaddr.sa_data)) == 0)
		{
			if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) 
			{
				perror("FPAD : rsattach SIOCGIFFLAGS");
				close(fd);
				return(-1);
			}

			close(fd);

			if (ifr.ifr_flags & IFF_UP) 
			{
				/* Already ok ... */
				return(0);
			}
			else
			{
				/* must be UP */
				return(1);
			}
		}
	}

	/* No device for this address... Look for a free one */
	for (i = 0; i < 10; i++) 
	{
		sprintf(dev, "rose%d", i);
		strcpy(ifr.ifr_name, dev);
	
		if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) 
		{
			perror("rsattach: SIOCGIFFLAGS");
			close(fd);
			return(-1);
		}


		if (!(ifr.ifr_flags & IFF_UP)) 
		{
			close(fd);
			return(1);
		}
	}

	close(fd);

	return -1;
}

static int startiface(char *dev, char *address, struct hostent *hp)
{
	struct ifreq ifr;
	char addr[5];
	int fd;
	
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("rsattach: socket");
		return FALSE;
	}

	strcpy(ifr.ifr_name, dev);
	
	if (hp != NULL) {
		ifr.ifr_addr.sa_family = AF_INET;
		
		ifr.ifr_addr.sa_data[0] = 0;
		ifr.ifr_addr.sa_data[1] = 0;
		ifr.ifr_addr.sa_data[2] = hp->h_addr_list[0][0];
		ifr.ifr_addr.sa_data[3] = hp->h_addr_list[0][1];
		ifr.ifr_addr.sa_data[4] = hp->h_addr_list[0][2];
		ifr.ifr_addr.sa_data[5] = hp->h_addr_list[0][3];
		ifr.ifr_addr.sa_data[6] = 0;

		if (ioctl(fd, SIOCSIFADDR, &ifr) < 0) {
			perror("rsattach: SIOCSIFADDR");
			return FALSE;
		}
	}

	if (rose_aton(address, addr) == -1)
		return FALSE;

	ifr.ifr_hwaddr.sa_family = ARPHRD_ROSE;
	memcpy(ifr.ifr_hwaddr.sa_data, addr, 5);
	
	if (ioctl(fd, SIOCSIFHWADDR, &ifr) != 0) {
		perror("rsattach: SIOCSIFHWADDR");
		return FALSE;
	}

	ifr.ifr_mtu = ROSE_MTU;

	if (ioctl(fd, SIOCSIFMTU, &ifr) < 0) {
		perror("rsattach: SIOCSIFMTU");
		return FALSE;
	}

	if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
		perror("rsattach: SIOCGIFFLAGS");
		return FALSE;
	}

	ifr.ifr_flags &= IFF_NOARP;
	ifr.ifr_flags |= IFF_UP;
	ifr.ifr_flags |= IFF_RUNNING;

	if (ioctl(fd, SIOCSIFFLAGS, &ifr) < 0) {
		perror("rsattach: SIOCSIFFLAGS");
		return FALSE;
	}
	
	close(fd);
	
	return TRUE;
}

/* Creates a new rsports file */
static void create_rsports(void)
{	
	FILE *fptr;
	time_t sec = time(NULL);
	
	fptr = fopen(CONF_RSPORTS_FILE, "w");
	if (fptr == NULL)
	{
		fprintf(stderr, "FPAD : cannot create rsports file\n");
		return;
	}
	fprintf(fptr,
		"# %s\n"
		"#\n"
		"# Automatic generation by fpad - %s"
		"#\n"
		"# The format of this file is:\n"
		"#\n"
		"# name address     description\n"
		"#\n",
		CONF_RSPORTS_FILE, ctime(&sec));
	fclose(fptr);
}

static void add_rsports(char *name, char *address)
{
	FILE *fptr;
	
	fptr = fopen(CONF_RSPORTS_FILE, "a");
	if (fptr == NULL)
	{
		fprintf(stderr, "FPAD : cannot find rsports file\n");
		return;
	}
	fprintf(fptr, "%s  %s  ROSE port %s\n", name, address, address+4);
	fclose(fptr);
}

char *l3_attach(char *rsaddr, struct hostent *hp, int verbose)
{
	int result;
	static char dev[64];

	result = getfreedev(dev, rsaddr);

	if (result == -1) 
	{
		fprintf(stderr, "FPAD : cannot find free ROSE device\n");
		return(NULL);
	}
	
	add_rsports(dev, rsaddr);

	if (!startiface(dev, rsaddr, hp))
		return(NULL);		

	if (verbose)
	{
		if (result == 0)
			printf("FPAD : ROSE address %s already bound to device %s\n", rsaddr, dev);
		else
			printf("FPAD : ROSE address %s bound to device %s\n", rsaddr, dev);
	}
	
	return(dev);
}

/* Returns the user port of a callsign or the default port */
char *user_port(ax25_address *addr)
{
	luser_t *l = cfg.luser;
	ax25_address buf;

	while (l)
	{
		if (!ax25_aton_entry(l->call, (char *)&buf) && !ax25_cmp(addr, &buf))
		{
			return(l->port);
		}
		l = l->next;
	}
	return(cfg.def_port);
}
