/* 
 * ../stats/fpacroute.c
 *
 * FPAC project
 *
 */
#include <time.h>
/*#include <sys/types.h>*/
/*#include <sys/socket.h>*/
#include <linux/if_ether.h>
/*#include <netinet/in.h>*/
#include <arpa/inet.h>
#include <sys/ioctl.h>
/*#include <sys/stat.h>*/
/*#include <netdb.h>*/

/*
 *
 * fpacroute.c
 *
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
/*#include <fcntl.h>*/
#include <ctype.h>

#include "ax25compat.h"
#include "wp.h"
#include "fpac.h"

#ifndef SOL_ROSE
#define SOL_ROSE 260
#endif
#ifndef SOL_AX25
#define SOL_AX25 257
#endif

cfg_t cfg;

void free_routes(void);
/* static void SignalHUP(int); */
static void SignalTERM(int);

struct route {
	char addr[11];
	int mask;
	int ndigi;
	char node[3][10];
	struct route *next;
};

struct route *head = NULL;
/*
static void SignalHUP(int code)
{
}
*/
static void SignalTERM(int code)
{
	syslog(LOG_INFO, "terminating on SIGTERM\n");
	closelog();
	exit(0);
}

void header(int fd)
{
	if ((write(fd, "\r  1st --Tries-- 3rd  L2Call       L3Call/Address    Locator City\r", 66)) < 0)
	{
		if (errno)
			perror("FPAC write error:");
	}
}
/*
static int callcmp(char *ref, char *call)
{
	char *s;
	char str[40];

	if (strchr(ref, '-') == NULL)
	{
		/ * No SSID in reference. Match any SSID * /
		strcpy(str, call);
		if ((s = strchr(str, '-')) != NULL)
			*s = '\0';
		call = str;
	}
	return (strcasecmp(ref, call));
}
*/

int node_is_connected(char *call)
{
	struct proc_rs_neigh *p, *list;
	int state = 0;

	if ((list = read_proc_rs_neigh()) == NULL)
	{
		if (errno)
			perror("FPAC fpacroute ERROR node_is_connected: read_proc_rs_neigh");
		return -1;
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
/*	node_t *n;*/
/* DEBUG F6BVP
	char text[256]; */

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
		node_t * n = cfg.node;

/* DEBUG F6BVP
 * sprintf(text, "n->call '%s' f->addr %s mask %d\n", n->call, f->addr, mask);
 * write (fd, text, strlen(text));		
*/
		/* Search the address of the node in the configuration file */
			while (n)
			{
			for (nroute=0; nroute < f->ndigi; nroute++)
				{	
				strtok(n->call, " \t,;");
				if (strcasecmp(f->node[nroute], n->call) == 0)
				{
/* DEBUG F6BVP
	sprintf(text,"route [%d] node '%s' mask '%d' dest '%s' ", nroute, f->node[nroute], mask, n->call);
	write (fd,text, strlen(text));
*/
		/* Only return address of a connected neighbour */ 
				if (node_is_connected(n->call))
					{	
/* DEBUG F6BVP
	sprintf(text, " CONNECTED\n");
	write (fd, text, strlen(text));	
*/
					strcpy(retaddr, n->dnic);
					strcat(retaddr, n->addr);
					return retaddr;
					}
/* DEBUG F6BVP
	sprintf (text, "NOT CONNECTED\n");				
	write (fd, text, strlen(text));
*/
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

int (is_local(char *addr))
{
	int found = 0;
	struct proc_rs *rp, *rlist;

	if ((rlist = read_proc_rs ()) == NULL && errno != 0)
		fprintf(stderr, "read_proc_rs %s\n", strerror(errno));

	for (rp = rlist; rp != NULL; rp = rp->next)
	{
		if (strcmp (rp->dest_addr, "*") == 0 && strcmp (rp->dest_call, "*") == 0)
		{
			if (strcmp(rp->src_addr, addr) == 0)
			{
				syslog(LOG_INFO,"FPAD is_local() addr : %s\n",addr);
				found = 1;
				break;
			}
		}
	}

	free_proc_rs (rlist);

	return found;
}

#define NB_TESTS 3
void check(int fd, ax25_address *destcall, ax25_address *destaddr, int nbdigis, int test)
{
	int i, j , n, s, nb;
	double sec[NB_TESTS];
	int addrlen;
	char *ptr;
	char *addr;
	char *daddr;
	char buf[256];
	char str[256];
	char txt[256];
	char roseaddr[14];
	struct timeval tc, tv;
	struct timezone tz;
	struct full_sockaddr_rose rsbind, rsconnect;

	s = -1;
	
	if (test)
	{
		for (;;)
		{
			alarm(60);
			nb = read(fd, buf, sizeof(buf));
			alarm(0);
			if (nb == -1)
				return;
			
			for (i = 0 ; i < 10 ; i++)
			{
				if (!isdigit(buf[i]))
				{
					if ((write(fd, buf, nb)) < 0)
					{
						if (errno)
                        			perror("FPAC write error:");
					}
					break;
				}
				roseaddr[i] = buf[i];
			}
			roseaddr[10] = '\0';
			
			if (i == 10)
				break;
		}
	}

	else
	{
		roseaddr[0] = '\0';
		
		if (nbdigis >= 2)
		{
			/* Get DNIC */
			strcpy(roseaddr, ax25_ntoa(&destaddr[1]));
			ptr = strrchr(roseaddr, '-');
			if (ptr)
				*ptr = '\0';
		}

		nb = strlen(roseaddr);
		if ((nb != 4) && (strspn(buf, "0123456789") != nb))
			strcpy(roseaddr, cfg.dnic);

		strcpy(roseaddr+4, ax25_ntoa(&destaddr[0]));
		ptr = strrchr(roseaddr, '-');
		if (ptr)
			*ptr = '\0';
	}
	
	if (is_local(roseaddr))
		return;
/*
 *
 * get address of opened route via connected adjacent node
 *
 */	
		daddr = get_address(fd, roseaddr);

		if (daddr == NULL)
		{
			sprintf(txt, "*** No route to %s in %s\r", roseaddr, cfg.callsign);
			if ((write(fd, txt, strlen(txt))) < 0)
			{
				if (errno)
                        	perror("FPAC write error:");
			}
			return;
		}

		/*
		 * Open the socket into the kernel.
		 */
		if ((s = socket(AF_ROSE, SOCK_SEQPACKET, 0)) < 0) 
		{
			fprintf(stderr, "FPAC fpacroute check() ERROR: cannot open ROSE socket, %s\r", strerror(errno));
			return;
		}

		memset(&rsbind, 0x00, sizeof(struct full_sockaddr_rose)); 
		memset(&rsconnect, 0x00, sizeof(struct full_sockaddr_rose));

		/* bind the socket */
		rsbind.srose_family = AF_ROSE;
		rsbind.srose_ndigis = 0;
		ax25_aton_entry(cfg.callsign, rsbind.srose_call.ax25_call);
		addr = rs_config_get_addr (NULL);
		rose_aton(addr, rsbind.srose_addr.rose_addr);

		addrlen = sizeof(struct full_sockaddr_rose);

		if (bind(s, (struct sockaddr *)&rsbind, addrlen) < 0) 
		{
			close(s);
			fprintf(stderr, "FPAC fpacroute check() bind: %s", strerror(errno));
			return;
		}

		rsconnect.srose_family = AF_ROSE;
		rsconnect.srose_ndigis = 0;

		if (ax25_aton_entry ("ROUTE", rsconnect.srose_call.ax25_call) == -1)
		{
			close(s);
			return;
		}

		if (rose_aton (daddr, rsconnect.srose_addr.rose_addr) == -1)
		{
			close(s);
			return;
		}

		if (connect(s, (struct sockaddr *)&rsconnect, sizeof(struct full_sockaddr_rose)) == -1)
		{
			close(s);
			sprintf(txt, "Connect %s / %s : %s\r", ax25_ntoa(destcall), roseaddr, strerror(errno));
			if ((write(fd, txt, strlen(txt))) < 0)
			{
				if (errno)
                        	perror("FPAC write error:");
			}
		}
		
	alarm(60);
	nb = read(s, buf, sizeof(buf));
	buf[nb] = '\0';
	alarm(0);
	
	for (i = 0 ; i < NB_TESTS ; i++)
	{
		gettimeofday(&tc, &tz);

		alarm(60);
		for (j = 0 ; j < 20 ; j++)
			str[j] = (j%26) + 'A';
		str[j] = '\r';
		if ((write(s, str, 21)) < 0)
		{
			if (errno)
                        perror("FPAC write error:");
		}
		n = read(s, str, sizeof(str));
		alarm(0);
		if (n == -1)
			return;

		gettimeofday(&tv, &tz);
	
		sec[i] = (double)(tv.tv_sec - tc.tv_sec);
		sec[i] += (double)((tv.tv_usec - tc.tv_usec) / 1000000.0);
	}

	nb = 0;
	for (i = 0 ; i < NB_TESTS ; i++)
		nb += sprintf(str+nb, "%6.3f ", sec[i]);
	
	strcat(str, buf);
	
	if ((write(fd, str, strlen(str))) < 0)
	{
		if (errno)
                perror("FPAC write error:");
	}

	strcat(roseaddr, "\r");	
	if ((write(s, roseaddr, 11)) < 0)
	{
		if (errno)
                perror("FPAC write error:");
	}

	for (;;)
	{
		alarm(60);
		nb = read(s, buf, sizeof(buf));
		if (nb <= 0)
			break;
		if ((write(fd, buf, nb)) < 0)
		{
			if (errno)
                        perror("FPAC write error:");
		}
		alarm(0);
	}

	close(s);
}

void route_menu(int fd)
{
	int nb;
	int addrok;
	char buf[256];
	char dnic[5];
	ax25_address digi[2];
	
	sprintf(buf,
		"\rFPAC trace route application.\n\nEnter address or callsign to check : ");
	for (;;)
	{
		if ((write(fd, buf, strlen(buf))) < 0)
		{
			if (errno)
                        perror("FPAC write error:");
		}
		alarm(600);
		nb = read(fd, buf, strlen(buf));
		alarm(0);
		
		if (nb <= 0)
			break;

		buf[nb] = '\0';
		while (iscntrl(buf[nb-1]))
		{
			buf[--nb] = '\0';
			if (nb == 0)
				break;
		}

		if (nb <= 0 || (nb == 1 && toupper(buf[0]) == 'B'))
			break;

		if (wp_check_call(buf) == 0)
		{
			ax25_address axcall;
			wp_t wpt;

			/* Possible callsign. Ask an address to the WP */
			addrok = 0;
			if (wp_open("TROUTE") == -1)
				break;
	
			ax25_aton_entry(buf, axcall.ax25_call);
			if (wp_get(&axcall, &wpt) == 0)
			{
				sprintf(buf, "Checking route %s found in WP\r", fpac2asc(&wpt.address.srose_addr));
				if ((write(fd, buf, strlen(buf))) < 0)
				{
					if (errno)
                        		perror("FPAC write error:");
				}
				strcpy(buf, rose_ntoa(&wpt.address.srose_addr));
				nb = 10;
			}
			else
			{
				sprintf(buf, "%s not found in WP\r", buf);
				if ((write(fd, buf, strlen(buf))) < 0)
				{
					if (errno)
                        		perror("FPAC write error:");
				}
				buf[0] = '\0';
				nb = 0;
			}
			wp_close();
		}
		
		if ((nb == 6 || nb == 10) && (strspn(buf, "0123456789") == nb))
		{
			/* Valid address */
			strcpy(dnic, cfg.dnic);
			if (nb == 10)
			{
				strncpy(dnic, buf, 4);
				ax25_aton_entry(buf+4, digi[0].ax25_call);
			}
			else
			{
				ax25_aton_entry(buf, digi[0].ax25_call);
			}
				
			ax25_aton_entry(dnic,  digi[1].ax25_call);
			header(fd);
			sprintf(buf, " 0.000  0.000  0.000 %-9s %9s/%s,%s %s %s\r",
				cfg.alt_callsign, cfg.callsign, cfg.dnic, cfg.address, cfg.locator, cfg.city);
			if ((write(fd, buf, strlen(buf))) < 0)
			{
				if (errno)
                        	perror("FPAC write error:");
			}
			check(fd, NULL, digi, 2, 0);
		}
		
		sprintf(buf, "\rEnter address or callsign to check : ");
	}
	
	if ((write(fd, "73\r", 3)) < 0)
	{
		if (errno)
                perror("FPAC write error:");
	}
	sleep(1);
	close(fd);
}

int my_listen(int domain, char *callsign, char *address, int digi)
{
	int fd;
	int addrlen;
	char *addr;
	struct sockaddr *pbind;
	struct full_sockaddr_ax25 axbind;
	struct full_sockaddr_rose rsbind;
	
	/* Create a listening socket on rose layer */
	if ((fd = socket(domain, SOCK_SEQPACKET, 0)) < 0) 
	{
		fprintf(stderr, "FPAC fpacroute my_listen() ERROR cannot open domain %d socket, %s\n", domain, strerror(errno));
		return -1;
	}
	
	switch (domain) 
	{
	case AF_ROSE:
		/* bind the socket */
		rsbind.srose_family = AF_ROSE;
		rsbind.srose_ndigis = 0;
		ax25_aton_entry(callsign, rsbind.srose_call.ax25_call);
		addr = rs_config_get_addr (NULL);
		rose_aton(addr, rsbind.srose_addr.rose_addr);

		addrlen = sizeof(struct full_sockaddr_rose);
		pbind = (struct sockaddr *)&rsbind;
		break;
		
	case AF_AX25:
		if (digi)
		{
		int yes = 1;
			if (setsockopt(fd, SOL_AX25, AX25_IAMDIGI, &yes, sizeof(yes)) == -1)
			{
				fprintf(stderr, "FPAC fpacroute my_listen() cannot setsockopt(AX25_IAMDIGI), %s\n", strerror(errno));
				close(fd);
				return(-1);
			}
		}

		axbind.fsa_ax25.sax25_family = AF_AX25;
		axbind.fsa_ax25.sax25_ndigis = 1;
		ax25_aton_entry(callsign, axbind.fsa_ax25.sax25_call.ax25_call);
		axbind.fsa_digipeater[0] = null_ax25_address;

		addrlen = sizeof(struct full_sockaddr_ax25);
		pbind = (struct sockaddr *)&axbind;
		break;
		
	default:
		close(fd);
		return -1;
	}
	
	if (bind(fd, pbind, addrlen) < 0) 
	{
		close(fd);
		fprintf(stderr, "FPAC fpacroute my_listen() bind: %s", strerror(errno));
		return(-1);
	}
	
	if (listen(fd, SOMAXCONN) < 0) 
	{
		close(fd);
		fprintf(stderr, "FPAC fpacroute my_listen() listen: %s", strerror(errno));
		return(-1);
	}
	
	return fd;
}

int main(int argc, char **argv)
{
	int n;
	int maxfd;
	int ax25_fd, digi_fd, rose_fd, route_fd;
	unsigned int addrlen;
	int new;
	char txt[256];
	struct full_sockaddr_rose rsorig;
	struct full_sockaddr_ax25 axorig, axdest;
	struct timeval tv;
	fd_set fdread;
	struct sigaction act, oact;
	
	if (ax25_config_load_ports() == 0) {
		fprintf(stderr, "FPAC fpacroute ERROR: problem with axports file\n");
		return 1;
	}

	if (rs_config_load_ports() == 0) {
		fprintf(stderr, "FPAC fpcaroute ERROR: problem with axports file\n");
		return 1;
	}

	/* Read FPAC configuration file */
	if (cfg_open(&cfg) != 0)
	{
		fprintf(stderr, "FPAC fpacroute ERROR: problem with fpac.conf file\n");
		return 1;
	}
	
	if (*cfg.trt_callsign == '\0')
		strcpy(cfg.trt_callsign, "ROUTE");

	/* Create a listening socket on ax25 layer */
	if ((ax25_fd = my_listen(AF_AX25, cfg.trt_callsign, NULL, 0)) < 0)
	{
		fprintf(stderr, "FPAC fpacroute ERROR: cannot open AX25 socket, %s\n", strerror(errno));
		fprintf(stderr,"\n");
		return 2;
	}
	
	/* Create a listening socket on ax25/digi layer */
	if ((digi_fd = my_listen(AF_AX25, cfg.trt_callsign, NULL, 1)) < 0)
	{
		fprintf(stderr, "FPAC fpacroute ERROR: cannot open AX25 digi socket, %s\n", strerror(errno));
		fprintf(stderr,"\n");
		return 2;
	}
	
	/* Create a listening socket on rose layer */
	if ((rose_fd = my_listen(AF_ROSE, cfg.trt_callsign, rs_config_get_addr (NULL), 0)) < 0)
	{
		fprintf(stderr, "FPAC fpacroute ERROR: cannot open ROSE socket, %s\n", strerror(errno));
		fprintf(stderr,"\n");
		return 2;
	}
	
	/* Create a listening socket on rose layer for ROUTE application */
	if ((route_fd = my_listen(AF_ROSE, "ROUTE", rs_config_get_addr (NULL), 0)) < 0)
	{
		fprintf(stderr, "FPAC fpacroute ERROR: cannot open ROSE route socket, %s\n", strerror(errno));
		fprintf(stderr,"\n");
		return 2;
	}
	
	if (!daemon_start(1))
	{
		fprintf(stderr, "FPAC fpacroute : cannot become a daemon\n");
		return 1;
	}

	for (;;)
	{
		FD_ZERO(&fdread);
		
		FD_SET(ax25_fd, &fdread);
		FD_SET(digi_fd, &fdread);
		FD_SET(rose_fd, &fdread);
		FD_SET(route_fd, &fdread);

		maxfd = route_fd;

/* Signals  
	act.sa_handler = SignalHUP;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGHUP, &act, &oact);
*/
	act.sa_handler = SignalTERM;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGTERM, &act, &oact);
	
		/* HERE IS THE SELECT !!! */
		tv.tv_usec = 0;
		tv.tv_sec  = 60;
		n = select(maxfd + 1, &fdread, NULL, NULL, &tv);
		
		if (n < 0)
			continue;

		usleep(5000);

		if (FD_ISSET(ax25_fd, &fdread))
		{
			/* New incoming connection */
			addrlen = sizeof(struct full_sockaddr_ax25);
			new = accept(ax25_fd, (struct sockaddr *)&axorig, &addrlen);
			if (new < 0) 
			{
				syslog(LOG_ERR, "FPAC fpacroute accept error %m\n");
					continue; 
			}

			if (fork() == 0)
			{
				/* child */
				route_menu(new);
				exit(0);
			}
			close(new);
		}
		
		if (FD_ISSET(rose_fd, &fdread))
		{
			/* New incoming connection */
			addrlen = sizeof(struct full_sockaddr_rose);
			new = accept(rose_fd, (struct sockaddr *)&rsorig, &addrlen);
			if (new < 0) 
			{
				syslog(LOG_ERR, "FPAC fpacroute accept error %m\n");
					continue; 
			}

			if (fork() == 0)
			{
				/* child */
				route_menu(new);
				exit(0);
			}
			close(new);
		}
		
		if (FD_ISSET(digi_fd, &fdread))
		{
			/* New incoming connection */
			addrlen = sizeof(struct full_sockaddr_ax25);
			new = accept(digi_fd, (struct sockaddr *)&axorig, &addrlen);
			if (new < 0) 
			{
				syslog(LOG_ERR, "FPAC fpacroute accept error %m\n");
					continue; 
			}

			header(new);
			sprintf(txt, " 0.000  0.000  0.000 %-9s %9s/%s,%s %s %s\r",
				cfg.alt_callsign, cfg.callsign, cfg.dnic, cfg.address, cfg.locator, cfg.city);
			if ((write(new, txt, strlen(txt))) < 0)
			{
				if (errno)
                        	perror("FPAC write error:");
			}
			
			if (axorig.fsa_ax25.sax25_ndigis < 2)
			{
				if ((write(new, "Destination missing\r", 19)) < 0)
				{
					if (errno)
                        		perror("FPAC write error:");
				}
				sleep(1);
				close(new);
			}
			
			if (fork() == 0)
			{
				/* child */
				addrlen = sizeof(struct full_sockaddr_ax25);
				getsockname(new, (struct sockaddr *)&axdest, &addrlen);

				check(new, &axdest.fsa_ax25.sax25_call, axorig.fsa_digipeater, axorig.fsa_ax25.sax25_ndigis, 0);
				sleep(1);
				close(new);
				exit(0);
			}
			close(new);
		}
		
		if (FD_ISSET(route_fd, &fdread))
		{
			/* New incoming connection */
			addrlen = sizeof(struct full_sockaddr_rose);
			new = accept(route_fd, (struct sockaddr *)&rsorig, &addrlen);
			if (new < 0) 
			{
				syslog(LOG_ERR, "FPAC fpacroute accept error %m\n");
				continue; 
			}

			/*
			addrlen = sizeof(struct full_sockaddr_rose);
			getsockname(new, (struct sockaddr *)&rsdest, &addrlen);
			*/

			if (fork() == 0)
			{
				/* child */
				sprintf(txt, "%-9s %9s/%s,%s %s %s\r",
					cfg.alt_callsign, cfg.callsign, cfg.dnic, cfg.address, cfg.locator, cfg.city);
				if ((write(new, txt, strlen(txt))) < 0)
				{
					if (errno)
                        		perror("FPAC write error:");
				}

				check(new, NULL, NULL, 0, 1);
				sleep(1);
				close(new);
				exit(0);
			}

			close(new);
		}
	}
	
	/* Will never occur */
	return 0;
}
