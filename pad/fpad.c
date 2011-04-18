
/******************************************************
 * fpad.c                                             *
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
#include <errno.h>
#include <signal.h>
#include <syslog.h>
#include <ctype.h>
#include <stdarg.h>

#include <time.h>
/*#include <sys/types.h>*/
/*#include <sys/fcntl.h>*/
#include <sys/file.h>
/*#include <sys/stat.h>*/
#include <sys/ioctl.h>
/*#include <sys/socket.h>*/
/*#include <netinet/in.h>*/

#ifndef SOL_ROSE
#define SOL_ROSE 260
#endif
#ifndef SOL_AX25
#define SOL_AX25 257
#endif

#include "ax25compat.h"

#define ENOIOCTLCMD 515

#define min(a,b) ((a) < (b) ? (a) : (b))

/* Variables are not extern in main */
#define extern

#include "fpad.h"
#include "wp.h"

#define PROC_RS_NEIGH_FILE      "/proc/net/rose_neigh"
#define PROC_RS_NODES_FILE      "/proc/net/rose_nodes"

#ifndef AX25_HBIT
#define	AX25_HBIT 0x80
#endif
#define CPROGRESS 1
#define CONNECTED 2
#define	WAITCALL  3

int log_valid = TRUE;
static void SignalHUP(int);
static void SignalTERM(int);
void clear_nodes(void);

user_t *head = NULL;

/*** Prototypes *******************/
static char *reason(unsigned char cause, unsigned char diagnostic);
static int new_alias_connection(alias_t *a, int verbose);
static int new_l2_connection(int fd, int verbose);
static int new_l3_connection(int fd, char *port, int verbose);
static int new_appli_connection(int fd, int accepted, char *appli);
static int new_socket(int type, char *callsign, char *addr, char *port, int digi, int pid);
static int rose_add(char *str);
static void end(user_t *p);

#ifdef __TCP__
static int new_tcp_connection(int fd, int verbose);
static int get_tcp_callsign(user_t *pl, char *str, int len);
#endif

#define BUFLEN 5120

/* #define MAXCOVER 32 */ 

void clear_nodes()
{
	ax25_address rose_call;
	struct rose_route_struct rs_node;
	int i;
	int s = 0;
	FILE *fp;
	char addr[10];
	char digi[10];
	char buff[80];
	char callsign[10], port[10];
	FILE *fn;
	char address[12], rmask[5], neigh1[10], neigh2[10], neigh3[10];
	int args;

	if ((s = socket(AF_ROSE, SOCK_SEQPACKET, 0)) < 0) {
		exit(1);
	}
		
	/* Obtain the Node Callsign */
	
	if (ioctl(s, SIOCRSL2CALL, &rose_call) == -1) {
		close(s);
		exit(1);
	}
	
	if ((fn = fopen(PROC_RS_NODES_FILE,"r")) == NULL) {
		close(s);
		exit(1);
	}

	while (fgets (buff, 80, fn)) {
		args=sscanf(buff,"%10s %4s %*s %9s %9s %9s", address, rmask, neigh1, neigh2, neigh3);
		if (strcmp (address,"address") == 0)
			continue;
		if (atoi (neigh1) == 1)
			continue;

	if ((fp = fopen(PROC_RS_NEIGH_FILE,"r")) == NULL) {
		exit(1);
	}
	
	while (fgets (buff, 80, fp)) {
	
		addr[0]='\0';
	       	callsign[0]='\0';
		port[0]='\0';
	       	digi[0]='\0';

		args = sscanf(buff,"%9s %9s %9s %*s %*s %*s %*s %*s %*s %9s", addr, callsign, port, digi);

		if (strcmp(addr, neigh1) == 0) {
			break;
		}
	}
	fclose(fp);

	rose_aton(address, rs_node.address.rose_addr); 

	rs_node.mask = atoi(rmask);

	for (i = atoi(rmask); i < 10; i++)
		address[i] = '0';

	rose_aton(address, rs_node.address.rose_addr); 

	strcpy(rs_node.device, port);

	ax25_aton_entry(callsign, rs_node.neighbour.ax25_call);
	
	ioctl(s, SIOCRSCLRRT, &rose_call); 

	ioctl(s, SIOCDELRT, &rs_node);
	
	} /* while not EOF RS_NODE_FILE */

	close(s);
}

static void SignalHUP(int code)
{
	if (conf_changed()) 
		exit(99);
}

static void SignalTERM(int code)
{
	clear_nodes();
	syslog(LOG_INFO, "terminating on SIGTERM\n");
	closelog();
	
	exit(0);
}

#ifdef __TCP__
static int msg(user_t *u, char *fmt, ...)
{
	char buf[512];
	char *ptr = buf;
	int len;
	va_list args;
	
	if ((u == NULL) || (u->fd == -1))
		return 0;

	va_start(args, fmt);
	vsprintf(buf+1, fmt, args);
	va_end(args);

	len = strlen(buf+1);

	switch(u->type)
	{
	case AF_INET:
		ptr = buf+1;
		break;

	case AF_AX25:
		buf[0] = 0xf0;
		ptr = buf;
		len += 1;
		break;

	case AF_ROSE:
		buf[0] = '\0';
		ptr = buf;
		len += 1;
		break;

	}
	write(u->fd, ptr, len);

	return 1;
}
#endif

static void add_queue(user_t *u, char *buf, int len)
{
	buf_t *b;
	buf_t *s;
	
	if (len <= 0)
		return;
		
	b = calloc(sizeof(buf_t), 1);
	if (b == NULL)
		return;
		
	b->data = malloc(len);
	if (b->data == NULL)
	{
		free(b);
		return;
	}
	memcpy(b->data, buf, len);
	b->len = len;
	b->next = NULL;
	
	if (u->dataq)
	{
		s = u->dataq;
		while (s->next)
			s = s->next;
		s->next = b;
	}
	else
		u->dataq = b;		
}

static buf_t *get_queue(user_t *u)
{
	buf_t *b;

	b = u->dataq;
	if (b)
		u->dataq = b->next;
	return b;
}

static void free_buf(buf_t *b)
{
	free(b->data);
	free(b);
}

static void free_queue(user_t *u)
{
	buf_t *b;

	while (u->dataq)
	{
		b = u->dataq;
		u->dataq = b->next;
		free_buf(b);
	}
}

static void free_user(user_t *u)
{
	free_queue(u);
	free(u);
}

int main(int argc, char **argv)
{
	unsigned int addrlen;
	int yes;
	int i;
	int n;
	int nb;
	int connection_done;
	int len;
	int daemon = 1;
	int verbose = 1;
	int maxfd;
	int fd_node[MAXCOVER];
/*	int fd_appli; */
#ifdef __TCP__
	int fd_tcp = -1;
#endif
	long lg;
	long size;
	buf_t *b;
	user_t *prev, *u;
	appli_t *ap;
	alias_t *a;
	port_t *p;
	addrp_t *d;
	cover_t *o;
	fd_set fdread, fdwrite;
	char *ptr;
	char *device;
	char address[11];
	char buffer[BUFLEN];
	struct timeval timeval;
	time_t wp_timeout = 0L;
	struct hostent *hp = NULL;
	struct full_sockaddr_rose wp;
	struct rose_facilities_struct facilities;
	struct rose_cause_struct rose_cause;
	char buf[80];
	char origin[80];
	struct sigaction act, oact;

	memset(&wp, 0x00, sizeof(struct full_sockaddr_rose));
	memset(&facilities, 0x00, sizeof(struct rose_facilities_struct));
	memset(&rose_cause, 0x00, sizeof(struct rose_cause_struct));

	openlog("fpad", LOG_DAEMON, LOG_INFO); /*LOG_PID, LOG_DAEMON);*/

	syslog(LOG_INFO, "starting FPAD");

	if ((argc == 2) && (strcmp(argv[1], "-d") == 0))
		daemon = 0;

	/* Signals */ 
	act.sa_handler = SignalHUP;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGHUP, &act, &oact);

	act.sa_handler = SignalTERM;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGTERM, &act, &oact);
	
	/* No signal !*/ 
	signal(SIGPIPE, SIG_IGN);
/*
	for (i = 1 ; i < NSIG ; i++)
	{
		if ((i == SIGABRT) || (i == SIGINT) || (i == SIGTERM))
			continue;
		else if (i == SIGHUP)
			signal(SIGHUP, sighup_handler);
		else
			signal(i, SIG_IGN);
*/	

	/* Load AX25 L2 configuration */
	if (ax25_config_load_ports() == 0)
	{
		fprintf(stderr, "problem with axports file\n");
		syslog(LOG_ERR, "problem with axports file\n");
		closelog();
		return(1);
	}

	/* Read FPAC configuration file */
	if (read_conf(0) == 0)
	{
		fprintf(stderr, "problem with fpac.conf file\n");
		syslog(LOG_ERR, "problem with fpac.conf file\n");
		closelog();
		return(1);
	}

#ifdef __TCP__
	/* Open TCPIP socket */
	if (cfg.inetport)
		if ((fd_tcp = new_socket(AF_INET, NULL, NULL, NULL, FALSE, FALSE)) < 1) {
			fprintf(stderr, "\nFPAD aborting\n");
			syslog(LOG_ERR, "aborting\n");
			closelog();
			return(1);
		}
#endif

	if ((hp = gethostbyname(cfg.def_addr)) == NULL)
	{
		fprintf(stderr, "invalid internet name/address - %s\n", cfg.def_addr);
		syslog(LOG_ERR, "invalid internet name/address - %s\n", cfg.def_addr);
		closelog();
		return 1;
	}

	/* Open AX25 L2 sockets */
	for (p = cfg.port ; (p != NULL) ; p = p->next)
	{
		/* Open an L2 digi socket with the FPAC main callsign */
		if ((p->fd_call = new_socket(AF_AX25, cfg.callsign, NULL, p->name, TRUE, TRUE)) < 1) {
			fprintf(stderr, "\nFPAD aborting\n");
			syslog(LOG_ERR, "aborting\n");
			closelog();
			return(1);
	}
		/* Open an L2 digi socket with the FPAC digi callsign */
		p->fd_digi = new_socket(AF_AX25, cfg.alt_callsign, NULL, p->name, TRUE, TRUE);

		/* Open an L2 application socket with the FPAC digi callsign */
		p->fd_appli = new_socket(AF_AX25, cfg.alt_callsign, NULL, p->name, FALSE, FALSE);

		/* Open L2 aliases sockets */
		for (a = p->alias ; a != NULL ; a = a->next)
			a->fd = new_socket(AF_AX25, a->alias, NULL, p->name, FALSE, TRUE);

		/* Open L2 applications sockets */
		for (ap = p->applis ; ap != NULL ; ap = ap->next)
			ap->fd = new_socket(AF_AX25, ap->call, NULL, p->name, FALSE, FALSE);
	}

	/* Open AX25 L3 sockets */
	/* fd_appli = -1; */

	for (i = 0 ; i < MAXCOVER ; i++)
		fd_node[i] = -1;

	if (l3_attach(cfg.fulladdr, hp, verbose) == 0)
		return(1);

	fd_node[0] = new_socket(AF_ROSE, NULL, cfg.fulladdr, "rose0", TRUE, TRUE);

	/*
	fpacnode application, callsign = NODE
	fd_appli[0] = new_socket(AF_ROSE, "NODE", cfg.fulladdr, "rose0", TRUE, FALSE);

	fpacnode application, callsign = digi
	fd_appli[1] = new_socket(AF_ROSE, fpac_digi, node_addr, "rose0", TRUE, FALSE);
	*/

	/* Open AX25 L3 sockets for AddPorts */
	for (d = cfg.addrp ; (d != NULL) ; d = d->next)
	{
		strcpy(address, cfg.fulladdr);
		memcpy(address+4, d->addr, 6);

		device = l3_attach(address, hp, verbose);
		if (device == NULL)
		{
			return(1);
		}
		d->fd = new_socket(AF_ROSE, NULL, address, device, TRUE, TRUE);
	}

	/* Open AX25 L3 sockets for coverage */
	for (i = 1, o = cfg.cover ; o != NULL ; i++, o = o->next)
	{
		if (i == MAXCOVER)
			break;
			
		strcpy(address, cfg.fulladdr);
		memcpy(address+4, o->addr, 6);

		device = l3_attach(address, hp, verbose);
		if (device == NULL)
		{
			return(1);
		}
		fd_node[i] = new_socket(AF_ROSE, NULL, address, device, TRUE, TRUE);
	}

	/* Get the size of one outq buffer */
	ioctl(fd_node[0], TIOCOUTQ, &size);

	size = BUFLEN;


	if ((daemon) && (!daemon_start(TRUE)) )
	{
		fprintf(stderr, "fpad : cannot become a daemon\n");
		closelog();
		return(1);
	}

	syslog(LOG_INFO,"FPAD becomes a daemon\n");

	/* Open the WP connection */
	if (wp_open("FPAD"))
	{
		fprintf(stderr, "\nFPAD cannot open WP service\n");
		syslog(LOG_ERR, "Cannot open WP service\n");
		closelog();
	/* No White Page server - Close all opened sockets before aborting */
		fprintf (stderr, "Closing opened sockets\n");
		syslog(LOG_ERR, "Closing  opened sockets");
		for (p = cfg.port ; (p != NULL) ; p = p->next)
		{	
			close (p->fd_call);
			close (p->fd_digi);
			close (p->fd_appli);
			for (a = p->alias ; a != NULL ; a = a->next)
				close (a->fd);
			for (ap = p->applis ; ap != NULL ; ap = ap->next)
				close (ap->fd);
		}
		close (fd_node[0]);
		for (d = cfg.addrp ; (d != NULL) ; d = d->next)
			close (d->fd);
		for (i = 1, o = cfg.cover ; o != NULL ; i++, o = o->next)
			close (fd_node[i]);
	/* No White Page server - Remove all ROSE neighbour nodes */
		fprintf (stderr, "Removing ROSE nodes\n");
		syslog(LOG_ERR, "Removing ROSE nodes");
		clear_nodes();
		fprintf (stderr, "FPAD aborting\n");
		syslog(LOG_ERR, "FPAD aborting");
		return(1);
	}
	
	syslog(LOG_INFO,"FPAD opened WP service\n");

	for (;;) 
	{
/*user_t * */	prev = NULL;

		maxfd = -1;
		FD_ZERO(&fdread);
		FD_ZERO(&fdwrite);

#ifdef __TCP__
		if (fd_tcp != -1)
		{
			FD_SET(fd_tcp, &fdread);
			if (fd_tcp > maxfd)
				maxfd = fd_tcp;
		}
#endif

		for (p = cfg.port ; p != NULL ; p = p->next)
		{
			FD_SET(p->fd_call, &fdread);
			if (p->fd_call > maxfd)
				maxfd = p->fd_call;

			FD_SET(p->fd_digi, &fdread);
			if (p->fd_digi > maxfd)
				maxfd = p->fd_digi;

			FD_SET(p->fd_appli, &fdread);
			if (p->fd_appli > maxfd)
				maxfd = p->fd_appli;

			for (a = p->alias ; a != NULL ; a = a->next)
			{
				FD_SET(a->fd, &fdread);
				if (a->fd > maxfd)
					maxfd = a->fd;
			}

			for (ap = p->applis ; ap != NULL ; ap = ap->next)
			{
				FD_SET(ap->fd, &fdread);
				if (ap->fd > maxfd)
					maxfd = ap->fd;
			}

		}

		for (d = cfg.addrp ; d != NULL ; d = d->next)
		{
			FD_SET(d->fd, &fdread);
			if (d->fd > maxfd)
				maxfd = d->fd;
		}

		for (i = 0 ; i < MAXCOVER ; i++)
		{
			if (fd_node[i] == -1)
				break;
			FD_SET(fd_node[i], &fdread);
			if (fd_node[i] > maxfd)
				maxfd = fd_node[i];
		}

		/* if (fd_appli != -1)
		{
			FD_SET(fd_appli, &fdread);
			if (fd_appli > maxfd)
				maxfd = fd_appli;
		} */

		for (u = head ; u != NULL ; )
		{
			if (u->fd == -1)
			{
				/* Remove from the list */
				if (prev == NULL)
				{
					head = u->next;
					free_user(u);
					u = head;
				}
				else
				{
					prev->next = u->next;
					free_user(u);
					u = prev->next;
				}
			}
			else
			{
				if (u->state == WAITCALL)
				{
					/* Wait for callsign */
					FD_SET(u->fd, &fdread);
				}

				/* read if output buffer available in u->peer */
				if ((u->peer) && (u->peer->fd != -1) && (u->peer->state == CONNECTED))
				{
					if (u->peer->queue < (size - 2048))
					{
						FD_SET(u->fd, &fdread);
					}
				}

				/* Wait for connection */
				if (u->peer && (u->peer->state == CPROGRESS))
				{
					/* Allow halting connection request */
					FD_SET(u->fd, &fdread);
				}

				if (u->state == CPROGRESS)
				{
					FD_SET(u->fd, &fdwrite);
				}

				if (u->queue > 2048)
					FD_SET(u->fd, &fdwrite);

				if (u->fd > maxfd)
					maxfd = u->fd;
				prev = u;
				u = u->next;
			}
		}

		/*
		 *
		 * HERE IS THE SELECT !!!
		 *
		 */

		timeval.tv_usec = 0;
		timeval.tv_sec  = 60;
		n = select(maxfd + 1, &fdread, &fdwrite, NULL, &timeval);
		
		if (n < 0)
			continue;

		/* Check the WP connection */
		if (!wp_is_open() && (wp_timeout < time(NULL)))
		{
			if (wp_open("FPAD"))
			{
				fprintf(stderr, "fpad : cannot open WP service\n");
				syslog(LOG_ERR, "Cannot open WP service\n");
				wp_timeout = time(NULL) + 60L;
			}
		}
			
		if (n == 0)		/* Timeout */
		{
			/* check_transits_wp(); */
			continue;
		}
#ifdef __TCP__
		/* Check for new connections on tcpip port */
		if (FD_ISSET(fd_tcp, &fdread))
		{
syslog(LOG_INFO,"new connection on AF_INET (%d)\n", fd_tcp);
			new_tcp_connection(fd_tcp, TRUE);
		}
#endif

		/* Check for new connections on all L2 ports */
		for (p = cfg.port ; (p != NULL) ; p = p->next)
		{
			if (FD_ISSET(p->fd_call, &fdread)) 
			{
				new_l2_connection(p->fd_call, FALSE);
			}

			if (FD_ISSET(p->fd_digi, &fdread)) 
			{
				new_l2_connection(p->fd_digi, TRUE);
			}

			if (FD_ISSET(p->fd_appli, &fdread)) 
			{
				new_appli_connection(p->fd_appli, FALSE, "fpacnode");
			}

			/* Aliases */
			for (a = p->alias ; a != NULL ; a = a->next)
			{
				if (FD_ISSET(a->fd, &fdread)) 
					new_alias_connection(a, TRUE);
			}

			/* Applications */
			for (ap = p->applis ; ap != NULL ; ap = ap->next)
			{
				if (FD_ISSET(ap->fd, &fdread)) 
					new_appli_connection(ap->fd, FALSE, ap->appli);
			}
		}

		/* Check for new connections on all L3 ports Coverage*/
		for (i = 0 ; i < MAXCOVER ; i++)
		{
			if (fd_node[i] == -1)
				break;
			if (FD_ISSET(fd_node[i], &fdread)) 
			{
				new_l3_connection(fd_node[i], NULL, TRUE);
			}
		}

		for (d = cfg.addrp ; (d != NULL) ; d = d->next)
		{
			if (FD_ISSET(d->fd, &fdread)) 
			{
				new_l3_connection(d->fd, d->port, TRUE);
			}
		}

		/*
		if (FD_ISSET(fd_appli, &fdread)) 
		{
			new_node_connection(fd_appli);
		}
		*/

		/* data to be read */
		for (u = head ; u != NULL ; u = u->next)
		{
			connection_done = 0;
			len = 0;
			
			if ((u->fd != -1) && (FD_ISSET(u->fd, &fdwrite)))
			{
				if (u->state == CPROGRESS)
				{
					connection_done = 1;
					u->state = CONNECTED;
				}
			}

			if ((u->fd != -1) && (FD_ISSET(u->fd, &fdread)))
			{
				len = read(u->fd, buffer+2, sizeof(buffer)-2);
/* DEBUG F6BVP */
				if (len <= 0)
/* DEBUG F6BVP */
/* DEBUG F6BVP */
				if (len == 0) {
					fprintf (stderr, "FPAD : CONNECTED read 0 length data\n");
				}
/* DEBUG F6BVP */
				if (len < 0)
				{
					connection_done = 0;
					len = -1;
				}
			}

			if (connection_done)
			{
				if (u->type == AF_ROSE)
				{
					addrlen = sizeof(struct full_sockaddr_rose);
					if (getpeername(u->fd, (struct sockaddr *)&wp, &addrlen) != -1) 
					{
						wp_update_addr(&wp);
					}

					if ((u->verbose) && (u->peer) && (u->peer->fd != -1))
					{
						buf[0] = 0xf0; /* Pid */
						sprintf(buf+1, "*** Connection done\r");
						write(u->peer->fd, buf, strlen(buf));
					}
					
					while ((b = get_queue(u)) != NULL)
					{
						write(u->fd, b->data, b->len);
						free_buf(b);
					}
				}
				else
				{
					/* Accept the L3 connection */
					if ((u->peer) && (u->peer->fd != -1))
					{
						yes = 1;
						ioctl(u->peer->fd, SIOCRSACCEPT, &yes);
					}
				}
			}

			if (len == -1)
			{
				/* Disconnection */
				if ((u->type == AF_ROSE) && (u->verbose) && (u->peer) && (u->peer->fd != -1))
				{
					rose_cause.cause      = ROSE_LOCAL_PROCEDURE;
					rose_cause.diagnostic = 0x12;

					ioctl(u->fd, SIOCRSGCAUSE, &rose_cause); 

					buffer[0] = 0xf0;
					if ((i = ioctl(u->fd, SIOCRSGFACILITIES, &facilities)) == ENOIOCTLCMD) {
						syslog(LOG_INFO,"\nNo system facilities available\n");
						fprintf(stderr, "\nNo system facilities available\n");
						syslog(LOG_ERR, "\nNo system facilities available\n");
					}
					else {
						syslog(LOG_INFO,"\nSystem facilities returned %d\n", i);
						fprintf(stderr, "\nSystem facilities returned %d\n", i);
						syslog(LOG_ERR, "\nSystem facilities returned %d\n", i);
					}
				if (u->state == CONNECTED)
					{
						origin[0] = '\0';
						
						if ((ioctl(u->fd, SIOCRSGFACILITIES, &facilities) != -1) &&
							(facilities.fail_call.ax25_call[0]))
						{
							sprintf(origin, " at %s @ %s",
								ax25_ntoa(&facilities.fail_call),
								fpac2asc(&facilities.fail_addr));
						}
						sprintf(buffer+1, "*** Disconnected from %s%s\r*** %02X%02X - %s\r",
							u->user,
							origin,
							rose_cause.cause, 
							rose_cause.diagnostic, 
							reason(rose_cause.cause, rose_cause.diagnostic));
			syslog(LOG_INFO, "%s\n", buffer);							
					}
					else
					{
			syslog(LOG_INFO,"facilities error %d : %s\n", errno, strerror(errno));
			syslog(LOG_INFO, "%s\n", buffer); 						
						sprintf(buffer+1, "*** No answer from %s\r*** %02X%02X - %s\r",
							u->user,
							rose_cause.cause, 
							rose_cause.diagnostic, 
							reason(rose_cause.cause, rose_cause.diagnostic));
					}
					write(u->peer->fd, buffer, strlen(buffer));
					
				}
				/* Local disconnection */
				if ((u->type == AF_AX25) && (u->verbose) && (u->peer) && (u->peer->fd != -1) && (u->peer->type == AF_AX25))
				{
					buffer[0] = 0xf0;
					if (u->state == CONNECTED)
					{
						sprintf(buffer+1, "*** Disconnected from %s\r",
							u->user);
					}
					else
					{
						sprintf(buffer+1, "*** No answer from %s\r",
							u->user);
					}
					write(u->peer->fd, buffer, strlen(buffer));
				}

				end(u);
				
			}
			else if (len > 0)
			{
				if ((u->peer) && (u->peer->fd != -1))
				{
					if (u->type == AF_AX25)
					{
						if (u->peer->type == AF_AX25)
						{
							/* Local connection */
							ptr = buffer+2;
						}
						else
						{
							/* Received is AX25 L3 */
							if (buffer[2] == (char)0xf0)
							{
								buffer[2] = 0;  /* Qbit = 0 */
								ptr = buffer + 2;
							}
							else
							{
								buffer[0] = 1;    /* Qbit = 1 */
								buffer[1] = 0x7f; /* Escape */
								ptr = buffer;
								len += 2;
							}
						}
					}
					else
					{
						if (buffer[2] == 0) 
						{		/* Q Bit not set */
							buffer[2] = 0xF0;
							ptr = buffer + 2;
						}
						else 
						{
							/* Lose the leading 0x7F */
							ptr = buffer + 3;
							len -= 1;
						}
					}
					
					nb = write(u->peer->fd, ptr, len);
					if (nb == -1)
					{
						/* Could not write, put in the peer queue */
						add_queue(u->peer, ptr, len);
					}
				}
#ifdef __TCP__
				else if (u->state == WAITCALL)
				{
					if (get_tcp_callsign(u, buffer+2,len))
						u->state = CONNECTED;
				}
#endif
			}
		}

		/* check queues */
		for (u = head ; u != NULL ; u = u->next)
		{
			if ((u->fd != -1) && (ioctl(u->fd, TIOCOUTQ, &lg) != -1))
			{
				u->queue = size - lg;
			}

		}

	}

	/* NOT REACHED */
	return 0;
}

static int new_socket(int domain, char *call, char *addr, char *port, int digi, int pidincl)
{
	int fd;
	int yes;
	int addrlen;
	int buflen;
	int type;
	char *p_name;
	struct full_sockaddr_rose rose;
	struct full_sockaddr_ax25 ax25;
#ifdef __TCP__
	struct sockaddr_in inet;
	memset(&inet, 0x00, sizeof(struct sockaddr_in));
#endif
	memset(&rose, 0x00, sizeof(struct full_sockaddr_rose));
	memset(&ax25, 0x00, sizeof(struct full_sockaddr_ax25));

	type = (domain == AF_INET) ? SOCK_STREAM : SOCK_SEQPACKET;

	if ((fd = socket(domain, type, 0)) < 0) 
	{
		syslog(LOG_ERR, "socket: %s\n", strerror(errno));
		return(-1);
	}

	buflen = BUFLEN;
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buflen, sizeof(buflen));
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buflen, sizeof(buflen));
	yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_DEBUG, &yes, sizeof(yes));

	switch (domain)
	{

#ifdef __TCP__
	case AF_INET:

		addrlen = sizeof(struct sockaddr_in);

		inet.sin_family      = AF_INET;
		inet.sin_addr.s_addr = INADDR_ANY;
		inet.sin_port        = htons(cfg.inetport);

		if (bind(fd, (struct sockaddr *)&inet, addrlen) < 0) 
		{
			close(fd);
			syslog(LOG_ERR, "bind INET: %s", strerror(errno));
			return(-1);
		}

		break;
#endif

	case AF_AX25:

		if (digi)
		{
			yes = 1;
			if (setsockopt(fd, SOL_AX25, AX25_IAMDIGI, &yes, sizeof(yes)) == -1)
			{
				syslog(LOG_ERR, "cannot setsockopt(AX25_IAMDIGI), %s\n", strerror(errno));
				close(fd);
				return(-1);
			}
		}

		if (pidincl)
		{
			yes = 1;
			if (setsockopt(fd, SOL_AX25, AX25_PIDINCL, &yes, sizeof(yes)) == -1)
			{
				syslog(LOG_ERR, "cannot setsockopt(AX25_PIDINCL), %s\n", strerror(errno));
				close(fd);
				return(-1);
			}
		}
		ax25.fsa_ax25.sax25_family = AF_AX25;
		ax25.fsa_ax25.sax25_ndigis = 1;
		ax25_aton_entry(call, ax25.fsa_ax25.sax25_call.ax25_call);
		if (*port == '*')
		{
			ax25.fsa_digipeater[0] = null_ax25_address;
		}
		else
		{
			p_name = ax25_config_get_addr (port);
			if (p_name)
				ax25_aton_entry(p_name, ax25.fsa_digipeater[0].ax25_call);
			else
			{
				fprintf(stderr, "Unknown port %s\n", port);
				return(-1);
			}
		}

		addrlen = sizeof(struct full_sockaddr_ax25);

		if (bind(fd, (struct sockaddr *)&ax25, addrlen) < 0) 
		{
			close(fd);
			syslog(LOG_ERR, "bind: %s", strerror(errno));
			return(-1);
		}

		break;

	case AF_ROSE:

		if (pidincl)
		{
			yes = 1;
			if (setsockopt(fd, SOL_ROSE, ROSE_QBITINCL, &yes, sizeof(yes)) == -1) 
			{
				close(fd);
				syslog(LOG_ERR, "cannot setsockopt(ROSE_QBITINCL) - %s\n", strerror(errno));
				return(-1);
			}
		}

		yes = 1;
		setsockopt(fd, SOL_ROSE, ROSE_DEFER, &yes, sizeof(yes));
		rose.srose_family = AF_ROSE;
		rose.srose_ndigis = 0;
		if (call)
			ax25_aton_entry(call, rose.srose_call.ax25_call);
		else
			rose.srose_call = null_ax25_address;
		rose_aton(addr, rose.srose_addr.rose_addr);

		addrlen = sizeof(struct full_sockaddr_rose);

		if (bind(fd, (struct sockaddr *)&rose, addrlen) < 0) 
		{
			close(fd);
			syslog(LOG_ERR, "bind: %s", strerror(errno));
			return(-1);
		}

		break;
	}

	if (listen(fd, SOMAXCONN) < 0) 
	{
		close(fd);
		syslog(LOG_ERR, "listen call=%s port=%s: %s", call, port, strerror(errno));
		return(-1);
	}

	return(fd);
}

static void end(user_t *p)
{
	/* Should be better to have the sockets closed when the queues are empty */
	sleep(1);
	if (p->fd != -1)
	{
		close(p->fd);
		p->fd = -1;
	}
	if (p->peer)
	{
		p = p->peer;
		if (p->fd != -1)
		{
			close(p->fd);
			p->fd = -1;
		}
	}
}

/* check if an address is a local address */
static int check_local(char *addr)
{
	addrp_t	*d;
	char fulladdr[11];

	strcpy(fulladdr, cfg.fulladdr);
	if (strcmp(addr, fulladdr) == 0)
		return 1;

	for (d = cfg.addrp ; d != NULL ; d = d->next)
	{
		strncpy(fulladdr+4, d->addr, 6);
		if (strcmp(fulladdr, addr) == 0)
			return 1;
	}
	return 0;
}

/* Returns the rose address defined for the AX25 port */
char *get_rose_addr(struct full_sockaddr_ax25 *ax25)
{
	addrp_t	*d;
	char *portname;
	static char address[40];

	strcpy(address, cfg.fulladdr);

	portname = ax25_config_get_port(&(ax25->fsa_digipeater[0]));
	if (portname)
	{
		for (d = cfg.addrp ; d != NULL ; d = d->next)
		{
			if (strcmp(d->port, portname) == 0)
			{
				strcpy(address+4, d->addr);
				break;
			}
		}
	}
	return(address);
}

static int new_l2_connection(int fd, int verbose)
{
	int i;
	int n;
	int len;
	int wp;
	int new;
	int yes;
	int rsfd;
	int npos;
	int ndigi;
	int buflen;
	unsigned int addrlen;
	int ask_wp;
	char *ptr;
	char *pdig;
	char dnic[5];
	char digi[10];
	char rsaddr[11];
	char str[20];
	char text[256];
	wp_t wpt;
	user_t *pl2, *pl3;
	struct full_sockaddr_rose wpu;
	struct full_sockaddr_ax25 rec, exp;
	struct full_sockaddr_rose rosebind, roseconnect;
	struct full_sockaddr_rose wpaddr; 
	struct rose_cause_struct rose_cause;

	memset(&wpu, 0x00, sizeof(struct full_sockaddr_rose));
	memset(&rec, 0x00, sizeof(struct full_sockaddr_ax25));
	memset(&exp, 0x00, sizeof(struct full_sockaddr_ax25));
	memset(&wpaddr, 0x00, sizeof(struct full_sockaddr_rose));
	memset(&rosebind, 0x00, sizeof(struct full_sockaddr_rose));
	memset(&roseconnect, 0x00, sizeof(struct full_sockaddr_rose));
	memset(&rose_cause, 0x00, sizeof(struct rose_cause_struct));

	yes = TRUE;
	ioctl(fd, FIONBIO, &yes);

	addrlen = sizeof(struct full_sockaddr_ax25);
	new = accept(fd, (struct sockaddr *)&exp, &addrlen);

	yes = FALSE;
	ioctl(fd, FIONBIO, &yes);

	if (new < 0) 
	{
		if (errno == EWOULDBLOCK)
			return(-1);	/* It's gone ??? */

		syslog(LOG_ERR, "accept error %m\n");
		return(-1); 
	}

	yes = 1;
	if (setsockopt(new, SOL_AX25, AX25_PIDINCL, &yes, sizeof(yes)) == -1)
	{
		syslog(LOG_ERR, "cannot setsockopt(AX25_PIDINCL) - %s\n", strerror(errno));
		close(new);
		return(-1);
	}

	addrlen = sizeof(rec);
	getsockname(new, (struct sockaddr *)&rec, &addrlen);

	pl2 = calloc(sizeof(user_t), 1);
	pl2->fd = new;
	pl2->queue = 0;
	pl2->type = AF_AX25;
	pl2->state = CONNECTED;
	pl2->peer = NULL;
	pl2->verbose = FALSE;
	strcpy(pl2->user, ax25_ntoa(&exp.fsa_ax25.sax25_call));

	/* Insert the new connection into the list */
	pl2->next = head;
	head = pl2;

	/* Build ROSE address */
	
	/* Look for the position of the node digi */
	for (ndigi = 0, npos = -1, n = exp.fsa_ax25.sax25_ndigis-1 ; n >=0 ; n--)
	{
		if (exp.fsa_digipeater[n].ax25_call[6] & AX25_HBIT)
		{
			npos = n;
			ndigi = n;
			break;
		}
	}
			
	strcpy(rsaddr, cfg.fulladdr);


	ask_wp = TRUE;

	if (ndigi > 0)
	{
		/* Rose address ??? */

		memset(&dnic, 0, sizeof(dnic));

		ptr = ax25_ntoa(&exp.fsa_digipeater[ndigi-1]);
		len = rose_add(ptr);

		/* Check if a country */
		strcpy(digi, ptr);
		pdig = strrchr(digi, '-');
		if (pdig)
			*pdig = '\0';

		if ((strlen(digi) == 3) && (ndigi > 0) && ((pdig = des2dnic(digi)) != NULL))
		{
			memcpy(dnic, pdig, 4);

			--ndigi;
			ptr = ax25_ntoa(&exp.fsa_digipeater[ndigi-1]);
			len = rose_add(ptr);
		}
		else if ((len == 4) && (ndigi > 0))
		{
			/* Check if dnic */			
			memcpy(dnic, ptr, 4);

			--ndigi;
			ptr = ax25_ntoa(&exp.fsa_digipeater[ndigi-1]);
			len = rose_add(ptr);
		}

		/* Check if it the dest digi is a node callsign from wp */
		if ((ndigi > 0) && (wp_get(&exp.fsa_digipeater[ndigi-1], &wpt) == 0) && (wpt.is_node) && (!wpt.is_deleted))
		{
			memcpy(rsaddr, rose_ntoa(&wpt.address.srose_addr), 10);
			ask_wp = FALSE;
			--ndigi;
		}
		else if ((len == 6) && (ndigi > 0))
		{
			if (*dnic)
				memcpy(rsaddr, dnic, 4);
			memcpy(rsaddr+4, ptr, 6);
			ask_wp = FALSE;
			--ndigi;
		}
	}

	/* Add the local callsign to the WP */
	wpu.srose_family = AF_ROSE;
	wpu.srose_ndigis = exp.fsa_ax25.sax25_ndigis - npos - 1;
		
	rose_aton (get_rose_addr(&rec), wpu.srose_addr.rose_addr);
	wpu.srose_call = exp.fsa_ax25.sax25_call;
	for ( n = 0 ; n < wpu.srose_ndigis ; n++)
		wpu.srose_digis[n] = exp.fsa_digipeater[exp.fsa_ax25.sax25_ndigis - n - 1];

	if ((wpu.srose_ndigis == 0) || (!wp_is_node(ax25_ntoa(&wpu.srose_digis[wpu.srose_ndigis-1]))))
		wp_update_addr(&wpu);
	
	roseconnect.srose_family = rosebind.srose_family = AF_ROSE;

	/* Get the ROSE address for this port */
	rose_aton (get_rose_addr(&rec), rosebind.srose_addr.rose_addr);
	rosebind.srose_call = exp.fsa_ax25.sax25_call;
	rose_aton (rsaddr, roseconnect.srose_addr.rose_addr);
	roseconnect.srose_call = rec.fsa_ax25.sax25_call;
	rosebind.srose_ndigis = exp.fsa_ax25.sax25_ndigis - npos - 1;
	for ( n = 0 ; n < rosebind.srose_ndigis ; n++)
		rosebind.srose_digis[n] = exp.fsa_digipeater[exp.fsa_ax25.sax25_ndigis - n - 1];

	roseconnect.srose_ndigis = ndigi;
	for ( n = 0 ; n < roseconnect.srose_ndigis ; n++)
		roseconnect.srose_digis[n] = exp.fsa_digipeater[ndigi-n-1];
	wp = 0;

	if (ask_wp && ndigi == 0)	/* WP only if no more digi */
	{
		/* No X25L3 routing. Trying via WP */

		if (wp_search(&rec.fsa_ax25.sax25_call, &wpaddr) == 0)
		{
			roseconnect = wpaddr;
			wp = 1;
		}
		else 
		{
			wp = 2;
		}


 }
	if (verbose)
	{
		static char *constr[] = { "Connecting", "WP routing", "Trying local" } ;

		text[0] = 0xf0;
		sprintf(text+1, "*** %s %s @ %s", constr[wp], ax25_ntoa(&rec.fsa_ax25.sax25_call), fpac2asc(&roseconnect.srose_addr));
		for (i = 0 ; i < roseconnect.srose_ndigis ; i++)
		{
			sprintf(str, ",%s", ax25_ntoa(&roseconnect.srose_digis[i]));
			strcat(text, str);
		}
		strcat(text, "\r");
		write(new, text, strlen(text));


	}

	/* Start the L3 connection */
	if ((rsfd = socket (AF_ROSE, SOCK_SEQPACKET, 0)) < 0)
	{
		syslog(LOG_ERR, "socket_rs\n");
		end(pl2);
		return(-1);
	}

	buflen = BUFLEN;
	setsockopt(rsfd, SOL_SOCKET, SO_RCVBUF, &buflen, sizeof(buflen));
	setsockopt(rsfd, SOL_SOCKET, SO_SNDBUF, &buflen, sizeof(buflen));

	yes = 1;
	if (setsockopt(rsfd, SOL_ROSE, ROSE_QBITINCL, &yes, sizeof(yes)) == -1) 
	{
		syslog(LOG_ERR, "cannot setsockopt(ROSE_QBITINCL) - %s\n", strerror(errno));
		end(pl2);
		return(-1);
	}

	addrlen = sizeof(struct full_sockaddr_rose);
	
	if (bind (rsfd, (struct sockaddr *) &rosebind, addrlen) == -1)
	{
		syslog(LOG_ERR, "bind_rs\n");
		close (rsfd);
		end(pl2);
      		return(-1);
	}

	/* Non blocking connection */
	yes = 1;
	ioctl(rsfd, FIONBIO, &yes);

	if (connect (rsfd, (struct sockaddr *) &roseconnect, addrlen) == -1)
	{
		if (errno != EINPROGRESS)
		{
			if (verbose)
			{
				switch (errno) 
				{
				case ENETUNREACH:
					ioctl(rsfd, SIOCRSGCAUSE, &rose_cause); 
					break;
				case EISCONN:
					rose_cause.cause      = ROSE_NUMBER_BUSY;
					rose_cause.diagnostic = 0x48;
					break;
				case EINVAL:
				default:
					rose_cause.cause      = ROSE_LOCAL_PROCEDURE;
					rose_cause.diagnostic = 0;
					break;
				}
				text[0] = 0xf0;
				sprintf(text+1, "*** Disconnected from %s\r*** %02X%02X - %s\r", 
					ax25_ntoa(&roseconnect.srose_call), 
					rose_cause.cause, 
					rose_cause.diagnostic, 
					reason(rose_cause.cause, rose_cause.diagnostic));
				write(new, text, strlen(text));
			}
			close (rsfd);
			end(pl2);
			return(-1);
		}
	}
  
	yes = 0;
	ioctl(rsfd, FIONBIO, &yes);

	pl3 = calloc(sizeof(user_t), 1);
	pl3->fd = rsfd;
	pl3->queue = 0;
	pl3->type = AF_ROSE;
	pl3->state = CPROGRESS;
	pl3->peer = pl2;
	pl3->verbose = verbose;
	strcpy(pl3->user, ax25_ntoa(&roseconnect.srose_call));


	/* Insert the new connection into the list */
	pl3->next = head;
	head = pl3;

	/* give L2 the L3 link */
	pl2->peer = pl3;

	return(new);
}

static int callcmp(char *call1, char *call2)
{
	int len1 = 0;
	int len2 = 0;
	char *ptr;

	ptr = strchr(call1, '-');
	if ((ptr) && (*(ptr+1) != '*'))
	{
		return strcasecmp(call1, call2);
	}

	ptr = strchr(call1, '-');
	if (ptr)
		len1 = ptr - call1;

	ptr = strchr(call2, '-');
	if (ptr)
		len2 = ptr - call2;

	return strncasecmp(call1, call2, (len2 > len1) ? len2 : len1);
}

/* Checks if the connection is for a local application */
static int appli_connection(char *callsign, char *appli)
{
	appli_t *a;

	for (a = cfg.appli ; a != NULL ; a = a->next)
	{
		if (callcmp(a->call, callsign) == 0)
		{
			strcpy(appli, a->appli);
			return(1);
		}
	}
	return(0);
}

static int new_l3_connection(int fd, char *port, int verbose)
{
	int n;
	int new;
	int yes;
	int axfd;
	int ndigi;
	int buflen;
	unsigned int addrlen;
	char *addr;
	char *p_name;
/*	char dnic[5];*/
	char application[256];
	user_t *pl2, *pl3;
	struct full_sockaddr_rose rsrec, rsexp;
	struct full_sockaddr_ax25 axbind, axconnect;
	struct rose_cause_struct rose_cause;

	memset(&rsrec, 0x00, sizeof(struct full_sockaddr_rose));
	memset(&rsexp, 0x00, sizeof(struct full_sockaddr_rose));
	memset(&axbind, 0x00, sizeof(struct full_sockaddr_ax25));
	memset(&axconnect, 0x00, sizeof(struct full_sockaddr_ax25));
	memset(&rose_cause, 0x00, sizeof(struct rose_cause_struct));

	memset(application, 0x00, 256 * sizeof(char));

	yes = 1;
	ioctl(fd, FIONBIO, &yes);

	addrlen = sizeof(struct full_sockaddr_rose);
	new = accept(fd, (struct sockaddr *)&rsexp, &addrlen);
	if (new < 0) 
	{
		if (errno == EWOULDBLOCK)
			return(-1);	/* It's gone ??? */

		syslog(LOG_ERR, "new_l3_connection() accept error %m\n");
			return(-1); 
	}
	yes = 0;
	ioctl(fd, FIONBIO, &yes);

	addrlen = sizeof(rsrec);
	getsockname(new, (struct sockaddr *)&rsrec, &addrlen);

	/* Check if it is a local application */
	if (appli_connection(ax25_ntoa(&rsrec.srose_call), application))
	{
		yes = FALSE;
		if (setsockopt(new, SOL_ROSE, ROSE_QBITINCL, &yes, sizeof(yes)) == -1) 
		{
			close(new);
			syslog(LOG_ERR, "new_l3_connection() cannot setsockopt(ROSE_QBITINCL) - %s\n", strerror(errno));
			return(-1);
		}
		return new_appli_connection(new, TRUE, application);
	}

	/* Add requester in the WP */
	if (wp_update_addr(&rsexp) != 0) 
		syslog(LOG_ERR, "new_l3_connection() cannot create/update wprecord\n");


	yes = 1;
	if (setsockopt(new, SOL_ROSE, ROSE_QBITINCL, &yes, sizeof(yes)) == -1) 
	{
		syslog(LOG_ERR, "new_l3_connection() cannot setsockopt(ROSE_QBITINCL) - %s\n", strerror(errno));
		close(fd);
		return(-1);
	}

	pl3 = calloc(sizeof(user_t), 1);
	pl3->fd = new;
	pl3->queue = 0;
	pl3->type = AF_ROSE;
	pl3->state = CONNECTED;
	pl3->peer = NULL;
	pl3->verbose = FALSE;
	strcpy(pl3->user, ax25_ntoa(&rsexp.srose_call));


	/* Insert the new connection into the list */
	pl3->next = head;
	head = pl3;

	/* Build AX25 structures */
	axbind.fsa_ax25.sax25_family = AF_AX25;
	axbind.fsa_ax25.sax25_ndigis = 1;
	axbind.fsa_ax25.sax25_call   = rsexp.srose_call;

	if (port)
		p_name = port;
	else
		p_name = user_port(&rsrec.srose_call);

	if ((addr = ax25_config_get_addr(p_name)) == NULL) 
	{
		syslog(LOG_ERR, "new_l3_connection() invalid AX.25 port name - %s\n", p_name);
		return(-1);
	}

	if (ax25_aton_entry(addr, axbind.fsa_digipeater[0].ax25_call) == -1) 
	{
		return(-1);
	}


	axconnect.fsa_ax25.sax25_family = AF_AX25;
	axconnect.fsa_ax25.sax25_call   = rsrec.srose_call;

	ndigi = 0;

	/* The path at the far end has a digi in it. */
	for (n = 0 ; n < rsexp.srose_ndigis ; n++) 
	{
		axconnect.fsa_digipeater[ndigi] = rsexp.srose_digis[n];


		axconnect.fsa_digipeater[ndigi].ax25_call[6] |= AX25_HBIT;
		ndigi++;
	}

	addr = rose_ntoa(&rsexp.srose_addr);


	/* Check if local address used */

	/* if (strcmp(addr, cfg.fulladdr) != 0) */
	if (!check_local(addr))
	{
		/*	Incoming call has a different DNIC */
		if (memcmp(rsexp.srose_addr.rose_addr, rsrec.srose_addr.rose_addr, 2) != 0)
		{
			char dnic[5];
			
			memcpy(dnic, addr, 4);
			dnic[4] = '\0';
			if (ax25_aton_entry(dnic, axconnect.fsa_digipeater[ndigi].ax25_call) == -1) 							{
				syslog(LOG_ERR, "new_l3_connection() invalid dnic - %s\n", dnic);
				return(-1);
			}

			axconnect.fsa_digipeater[ndigi].ax25_call[6] |= AX25_HBIT;
			ndigi++;		
		}
	
		/* Put the remote address sans DNIC into the digi chain. */
		addr = rose_ntoa(&rsexp.srose_addr);
		if (ax25_aton_entry(addr + 4, axconnect.fsa_digipeater[ndigi].ax25_call) == -1)
		{
			syslog(LOG_ERR, "new_l3_connection() invalid callsign - %s\n", addr + 4);
			return(-1);
		}
		axconnect.fsa_digipeater[ndigi].ax25_call[6] |= AX25_HBIT;
		ndigi++;		
	}
	

	/* And my local ROSE callsign. */
	if (ax25_aton_entry(cfg.alt_callsign, axconnect.fsa_digipeater[ndigi].ax25_call) == -1)
	{
		syslog(LOG_ERR, "new_l3_connexion() invalid callsign - %s\n", cfg.alt_callsign);
		return(-1);
	}

	
	axconnect.fsa_digipeater[ndigi].ax25_call[6] |= AX25_HBIT;
	ndigi++;

	/* A digi has been specified for this end. */
	for (n = 0 ; n < rsrec.srose_ndigis ; n++) 
	{
		axconnect.fsa_digipeater[ndigi] = rsrec.srose_digis[n];
		axconnect.fsa_digipeater[ndigi].ax25_call[6] &= ~AX25_HBIT;
		ndigi++;
	}

	axconnect.fsa_ax25.sax25_ndigis = ndigi;

	/* Start the L2 connection */
	if ((axfd = socket (AF_AX25, SOCK_SEQPACKET, 0)) < 0)
	{
		end(pl3);
		return(-1);
	}
	
	
	buflen = BUFLEN;
	setsockopt(axfd, SOL_SOCKET, SO_RCVBUF, &buflen, sizeof(buflen));
	setsockopt(axfd, SOL_SOCKET, SO_SNDBUF, &buflen, sizeof(buflen));

	yes = 1;
	if (setsockopt(axfd, SOL_AX25, AX25_IAMDIGI, &yes, sizeof(yes)) == -1) 
	{
		syslog(LOG_ERR, "new_l3_connection() cannot setsockopt(AX25_IAMDIGI), %s\n", strerror(errno));
		close(axfd);
		return(-1);
	}

	yes = 1;
	if (setsockopt(axfd, SOL_AX25, AX25_PIDINCL, &yes, sizeof(yes)) == -1)
	{
		syslog(LOG_ERR, "new_l3_connection() cannot setsockopt(AX25_PIDINCL), %s\n", strerror(errno));
		close(axfd);
		end(pl3);
		return(-1);
	}

	addrlen = sizeof(struct full_sockaddr_ax25);
	
	if (bind (axfd, (struct sockaddr *) &axbind, addrlen) == -1)
	{
		syslog(LOG_ERR, "new_l3_connection() cannot bind axfd\n");
		close (axfd);
		end(pl3);
      		return(-1);
	}

	/* Non blocking connection */
	yes = 1;
	ioctl(axfd, FIONBIO, &yes);

	if (connect (axfd, (struct sockaddr *) &axconnect, addrlen) == -1)
	{
		if (errno != EINPROGRESS)
		{
			switch (errno) 
			{
			case EADDRINUSE:
				rose_cause.cause      = ROSE_NUMBER_BUSY;
				rose_cause.diagnostic = 0x48;
				break;
			case ECONNREFUSED:
				rose_cause.cause      = ROSE_NUMBER_BUSY;
				rose_cause.diagnostic = 0;
				break;
			case ETIMEDOUT:
				rose_cause.cause      = ROSE_SHIP_ABSENT;
				rose_cause.diagnostic = 0;
				break;
			default:
				rose_cause.cause      = ROSE_REMOTE_PROCEDURE;
				rose_cause.diagnostic = errno;
				break;
			}

			ioctl(new, SIOCRSSCAUSE, &rose_cause);

			close (axfd);
			end(pl3);
			return(-1);
		}
	}
 
	yes = 0;
	ioctl(axfd, FIONBIO, &yes);

	pl2 = calloc(sizeof(user_t), 1);
	pl2->fd = axfd;
	pl2->queue = 0;
	pl2->type = AF_AX25;
	pl2->state = CPROGRESS;
	pl2->peer = pl3;
	pl2->verbose = verbose;
	strcpy(pl2->user, ax25_ntoa(&axconnect.fsa_ax25.sax25_call));


	/* Insert the new connection into the list */
	pl2->next = head;
	head = pl2;

	/* give L3 the L2 link */
	pl3->peer = pl2;

	return(new);
}

static int new_alias_connection(alias_t *a, int verbose)
{
	int n, len, npos;
	int ok;
	int new;
	int yes;
	int rsfd;
	int ndigi;
	int buflen;
	unsigned int addrlen;
	char *ptr;
	char *call;
	int dnic[5];
	char rsaddr[11];
	char text[256];
	char *stmp;
	user_t *pl2, *pl3;
	struct full_sockaddr_rose wp;
	struct full_sockaddr_ax25 rec;
	struct full_sockaddr_ax25 exp;
	struct full_sockaddr_rose rosebind, roseconnect;
	struct rose_cause_struct rose_cause;

	memset(&wp, 0x00, sizeof(struct full_sockaddr_rose));
	memset(&rec, 0x00, sizeof(struct full_sockaddr_ax25));
	memset(&exp, 0x00, sizeof(struct full_sockaddr_ax25));
	memset(&rosebind, 0x00, sizeof(struct full_sockaddr_rose));
	memset(&roseconnect, 0x00, sizeof(struct full_sockaddr_rose));
	memset(&rose_cause, 0x00, sizeof(struct rose_cause_struct));
/*
 * printf("new_alias\n");
 *
 */
	yes = TRUE;
	ioctl(a->fd, FIONBIO, &yes);

	addrlen = sizeof(struct full_sockaddr_ax25);
	new = accept(a->fd, (struct sockaddr *)&exp, &addrlen);

	yes = FALSE;
	ioctl(a->fd, FIONBIO, &yes);

	if (new < 0) 
	{
		if (errno == EWOULDBLOCK)
			return(-1);	/* It's gone ??? */

		syslog(LOG_ERR, "accept error %m\n");
		return(-1); 
	}

	yes = 1;
	if (setsockopt(new, SOL_AX25, AX25_PIDINCL, &yes, sizeof(yes)) == -1)
	{
		syslog(LOG_ERR, "cannot setsockopt(AX25_PIDINCL) - %s\n", strerror(errno));
		close(new);
		return(-1);
	}

	addrlen = sizeof(rec);
	getsockname(new, (struct sockaddr *)&rec, &addrlen);
            
	{
		/* Add the local callsign to the WP
		struct full_sockaddr_rose wp;
		int n, npos; */

		/* Look for the position of the node digi */
		for (ndigi = 0, npos = -1, n = exp.fsa_ax25.sax25_ndigis-1 ; n >=0 ; n--)
		{
			if (exp.fsa_digipeater[n].ax25_call[6] & AX25_HBIT)
			{
				npos = n;
				break;
			}
		}
			
		wp.srose_family = AF_ROSE;
		wp.srose_ndigis = exp.fsa_ax25.sax25_ndigis - npos - 1;
		
		rose_aton (get_rose_addr(&rec), wp.srose_addr.rose_addr);
		wp.srose_call = exp.fsa_ax25.sax25_call;
		for ( n = 0 ; n < wp.srose_ndigis ; n++)
			wp.srose_digis[n] = exp.fsa_digipeater[exp.fsa_ax25.sax25_ndigis - n - 1];

		if ((wp.srose_ndigis == 0) || (!wp_is_node(ax25_ntoa(&wp.srose_digis[wp.srose_ndigis-1]))))
			wp_update_addr(&wp);
	}
	
	pl2 = calloc(sizeof(user_t), 1);
	pl2->fd = new;
	pl2->queue = 0;
	pl2->type = AF_AX25;
	pl2->state = CONNECTED;
	pl2->peer = NULL;
	pl2->verbose = FALSE;
	strcpy(pl2->user, ax25_ntoa(&exp.fsa_ax25.sax25_call));

	/* Insert the new connection into the list */
	pl2->next = head;
	head = pl2;

	ndigi = -1;

	/* Build ROSE address */
	strcpy(rsaddr, cfg.fulladdr);

	stmp = strdup(a->path);

	/* callsign */
	call = strtok(stmp, " \t,");

	/* address */
	ptr = strtok(NULL, " \t,");
	if (ptr)
	{
		ok = FALSE;
		memset(&dnic, 0, sizeof(dnic));
		len = rose_add(ptr);

		/* Check if dnic */
		if ((len == 4) && (ndigi > 0))
		{
			memcpy(dnic, ptr, 4);
			ptr = strtok(NULL, " \t,");
			len = rose_add(ptr);
		}

		if (len == 6)
		{
			if (*dnic)
				memcpy(rsaddr, dnic, 4);
			memcpy(rsaddr+4, ptr, 6);
			ok = TRUE;
		}
	}

	if (verbose)
	{
		text[0] = 0xf0;
		sprintf(text+1, "*** Connecting %s @ %s", call, rsaddr);
		/*
		for (i = ndigi ; i >= 0 ; i--)
		{
			char str[20];
			sprintf(str, ",%s", ax25_ntoa(&exp.fsa_digipeater[ndigi]));
			strcat(text, str);
		}
		*/
		strcat(text, "\r");
		write(new, text, strlen(text));
	}

	/* Start the L3 connection */
	if ((rsfd = socket (AF_ROSE, SOCK_SEQPACKET, 0)) < 0)
	{
		syslog(LOG_ERR, "socket_rs\n");
		end(pl2);
		return(-1);
	}

	buflen = BUFLEN;
	setsockopt(rsfd, SOL_SOCKET, SO_RCVBUF, &buflen, sizeof(buflen));
	setsockopt(rsfd, SOL_SOCKET, SO_SNDBUF, &buflen, sizeof(buflen));

	yes = 1;
	if (setsockopt(rsfd, SOL_ROSE, ROSE_QBITINCL, &yes, sizeof(yes)) == -1) 
	{
		syslog(LOG_ERR, "cannot setsockopt(ROSE_QBITINCL) - %s\n", strerror(errno));
		end(pl2);
		free(stmp);
		return(-1);
	}

	roseconnect.srose_family = rosebind.srose_family = AF_ROSE;

	rosebind.srose_ndigis    = 0;
	rose_aton (cfg.fulladdr, rosebind.srose_addr.rose_addr);
	rosebind.srose_call = exp.fsa_ax25.sax25_call;

	roseconnect.srose_ndigis = 0;
	rose_aton (rsaddr, roseconnect.srose_addr.rose_addr);
	if (ax25_aton_entry(call, roseconnect.srose_call.ax25_call) == -1)
	{
		syslog(LOG_ERR, "ax25_aton_entry %s\n", roseconnect.srose_call.ax25_call);
		close (rsfd);
		end(pl2);
		free(stmp);
      		return(-1);
	}

	free(stmp);

	addrlen = sizeof(struct full_sockaddr_rose);
	
	if (bind (rsfd, (struct sockaddr *) &rosebind, addrlen) == -1)
	{
		syslog(LOG_ERR, "bind_rs\n");
		close (rsfd);
		end(pl2);
      		return(-1);
	}

	/* Non blocking connection */
	yes = 1;
	ioctl(rsfd, FIONBIO, &yes);

	if (connect (rsfd, (struct sockaddr *) &roseconnect, addrlen) == -1)
	{
		if (errno != EINPROGRESS)
		{
			if (verbose)
			{
				switch (errno) 
				{
				case ENETUNREACH:
					ioctl(rsfd, SIOCRSGCAUSE, &rose_cause); 
					break;
				case EISCONN:
				case EINVAL:
				default:
					rose_cause.cause      = ROSE_LOCAL_PROCEDURE;
					rose_cause.diagnostic = 0;
					break;
					break;
				}
				text[0] = 0xf0;
				sprintf(text+1, "*** Failure with %s\r*** %02X%02X - %s\r", 
					ax25_ntoa(&roseconnect.srose_call), 
					rose_cause.cause, 
					rose_cause.diagnostic, 
					reason(rose_cause.cause, rose_cause.diagnostic));
				write(new, text, strlen(text));
			}
			close (rsfd);
			end(pl2);
			return(-1);
		}
	}
  
	yes = 0;
	ioctl(rsfd, FIONBIO, &yes);

	pl3 = calloc(sizeof(user_t), 1);
	pl3->fd = rsfd;
	pl3->queue = 0;
	pl3->type = AF_ROSE;
	pl3->state = CPROGRESS;
	pl3->peer = pl2;
	pl3->verbose = verbose;
	strcpy(pl3->user, ax25_ntoa(&roseconnect.srose_call));

	/* Insert the new connection into the list */
	pl3->next = head;
	head = pl3;

	/* give L2 the L3 link */
	pl2->peer = pl3;

	return(new);
}

/* These functions are straight from *NOS */

static void expand_string (char *dest, char *src, char *call, int type)
{
	char tmpbuf[1024], def[64];
	char *p1, *p2, *cp;
	int n, skip;

	if (strchr (src, '%') == NULL)
	{
		strcpy (dest, src);
		return;
	}
	p1 = src;
	p2 = dest;
	while (*p1)
	{
		if (*p1 == '%')
		{
			p1++;
			skip = 1;
			def[0] = 0;
			if (*p1 == '{')
			{
				p1++;
				if ((cp = strchr (p1, '}')) != NULL)
				{
					skip = cp - p1 + 1;
					cp = p1;
					if (*++cp == ':')
					{
						cp++;
						n = min (skip - 3, 63);
						strncpy (def, cp, n);
						def[n] = 0;
					}
				}
			}
			switch (*p1)
			{
			case 'u':
			case 'U':
				strcpy (tmpbuf, call);
				if ((cp = strrchr (tmpbuf, '-')))
					*cp = 0;
				break;
			case 's':
			case 'S':
				strcpy (tmpbuf, call);
				break;
			case 't':
			case 'T':
				switch (type)
				{
				case AF_AX25:
					strcpy (tmpbuf, "ax25");
					break;
				case AF_NETROM:
					strcpy (tmpbuf, "netrom");
					break;
				case AF_ROSE:
					strcpy (tmpbuf, "rose");
					break;
				case AF_INET:
					strcpy (tmpbuf, "inet");
					break;
				case AF_UNSPEC:
					strcpy (tmpbuf, "host");
					break;
				}
				break;
			default:
				strcpy (tmpbuf, "%");
				break;
			}
			if (isalpha (*p1))
			{
				if (isupper (*p1))
					strupr (tmpbuf);
				else
					strlwr (tmpbuf);
			}
			strcpy (p2, tmpbuf);
			p2 += strlen (tmpbuf);
			p1 += skip;
		}
		else
			*p2++ = *p1++;
	}
	*p2 = 0;


	return;
}

static char *parse_string(char *str)
{
	char *cp = str;
	unsigned long num;

	while (*str != '\0' && *str != '\"') {
		if (*str == '\\') {
			str++;
			switch (*str++) {
			case 'n':
				*cp++ = '\n';
				break;
			case 't':
				*cp++ = '\t';
				break;
			case 'v':
				*cp++ = '\v';
				break;
			case 'b':
				*cp++ = '\b';
				break;
			case 'r':
				*cp++ = '\r';
				break;
			case 'f':
				*cp++ = '\f';
				break;
			case 'a':
				*cp++ = '\007';
				break;
			case '\\':
				*cp++ = '\\';
				break;
			case '\"':
				*cp++ = '\"';
				break;
			case 'x':
				--str;
				num = strtoul(str, &str, 16);
				*cp++ = (char) num;
				break;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
				--str;
				num = strtoul(str, &str, 8);
				*cp++ = (char) num;
				break;
			case '\0':
				return NULL;
			default:
				*cp++ = *(str - 1);
				break;
			};
		} else {
			*cp++ = *str++;
		}
	}
	if (*str == '\"')
		str++;		/* skip final quote */
	*cp = '\0';		/* terminate string */
	return str;
}

static int parse_args(char **argv, char *cmd)
{
	int ct = 0;
	int quoted;

	while (ct < 31)
	{
		quoted = 0;
		while (*cmd && isspace(*cmd))
			cmd++;
		if (*cmd == 0)
			break;
		if (*cmd == '"') {
			quoted++;
			cmd++;
		}
		argv[ct++] = cmd;
		if (quoted) {
			if ((cmd = parse_string(cmd)) == NULL)
				return 0;
		} else {
			while (*cmd && !isspace(*cmd))
				cmd++;
		}
		if (*cmd)
			*cmd++ = 0;
	}
	argv[ct] = NULL;
	return ct;
}

static int new_appli_connection(int fd, int accepted, char *appli)
{
	int n, npos;
	int new;
	int pid;
	int yes;
	unsigned int addrlen;
	char *ptr;
/*	char *argv[32];
	char dest[1024];
	char call[20];
	int type;

	union {
		struct full_sockaddr_ax25 sax;
		struct full_sockaddr_rose srose;
		struct sockaddr_in sin;
	} saddr;
	unsigned int slen = sizeof(saddr);
*/
	struct full_sockaddr_ax25 exp;
	struct full_sockaddr_ax25 rec;
	struct full_sockaddr_rose wp;

	memset(&wp, 0x00, sizeof(struct full_sockaddr_rose));
	memset(&rec, 0x00, sizeof(struct full_sockaddr_ax25));
	memset(&exp, 0x00, sizeof(struct full_sockaddr_ax25));
/*	memset(&saddr.sin, 0x00, sizeof(struct full_sockaddr_ax25));
	memset(dest, 0x00, 1024 * sizeof(char));
*/
/*	printf("new_appli\n");
	syslog(LOG_INFO,"new_appli");
*/	
	if (accepted)
	{
		new = fd;
	}
	else
	{
		yes = TRUE;
		ioctl(fd, FIONBIO, &yes);

		addrlen = sizeof(struct full_sockaddr_ax25);
		new = accept(fd, (struct sockaddr *)&exp, &addrlen);

		yes = FALSE;
		ioctl(fd, FIONBIO, &yes);

		if (new < 0) 
		{
			if (errno == EWOULDBLOCK)
				return(-1);	/* It's gone ??? */

			syslog(LOG_ERR, "accept error %m\n");
			return(-1); 
		}

		addrlen = sizeof(rec);
		getsockname(new, (struct sockaddr *)&rec, &addrlen);
            
		/* Look for the position of the node digi */
		for (npos = -1, n = exp.fsa_ax25.sax25_ndigis-1 ; n >=0 ; n--)
		{
			if (exp.fsa_digipeater[n].ax25_call[6] & AX25_HBIT)
			{
				npos = n;
				break;
			}
		}
			
		/* Add the local callsign to the WP */

		wp.srose_family = AF_ROSE;
		wp.srose_ndigis = exp.fsa_ax25.sax25_ndigis - npos - 1;
	
		rose_aton (get_rose_addr(&rec), wp.srose_addr.rose_addr);
		wp.srose_call = exp.fsa_ax25.sax25_call;
		for ( n = 0 ; n < wp.srose_ndigis ; n++)
			wp.srose_digis[n] = exp.fsa_digipeater[exp.fsa_ax25.sax25_ndigis - n - 1];

		if ((wp.srose_ndigis == 0) || (!wp_is_node(ax25_ntoa(&wp.srose_digis[wp.srose_ndigis-1]))))
			wp_update_addr(&wp);
	}

	pid = fork();

	switch (pid) 
	{
	case -1:
		syslog(LOG_ERR, "fork error %m");
		close(new);
		new = -1;
		break;

	case 0: {
		char *argv[32];
		char dest[1024];
		char call[20];
		int type;

		union {
			struct full_sockaddr_ax25	sax;
			struct full_sockaddr_rose	srose;
			struct sockaddr_in		sin;
		} saddr;
		unsigned int slen = sizeof(saddr);
		memset(&saddr.sin, 0x00, sizeof(struct full_sockaddr_ax25));
		memset(dest, 0x00, 1024 * sizeof(char));

		if (getpeername(new, (struct sockaddr *)&saddr, &slen) == -1)
			type = AF_UNSPEC;
		else
			type = saddr.sax.fsa_ax25.sax25_family;

		switch (type)
		{
		case AF_AX25:
			strcpy(call, ax25_ntoa(&saddr.sax.fsa_ax25.sax25_call));
			break;
		case AF_NETROM:
			strcpy(call, ax25_ntoa(&saddr.sax.fsa_ax25.sax25_call));
			break;
		case AF_ROSE:
			yes = 1;
			ioctl(new, SIOCRSACCEPT, &yes);
			strcpy(call, ax25_ntoa(&saddr.srose.srose_call));
			break;
		case AF_INET:
			ptr = getenv("CALL_TCP");
			if (ptr)
			{
				strcpy(call, ptr);
				break;
			}
		case AF_UNSPEC:
			strcpy(call, "fpad-0");
			break;
		}

		expand_string (dest, appli, call, type);
		parse_args(argv, dest);

		/* pid = text */
		dup2(new, STDIN_FILENO);
		dup2(new, STDOUT_FILENO);
		close(new);

		execvp(argv[0], argv);
		exit(1);
		}
	default:
		close(new);
		new = -1;
		break;
	}

	return(new);
}


#ifdef __TCP__
static int new_tcp_connection(int fd, int verbose)
{
	int new;
	int yes;
	user_t *pl2;
	struct sockaddr_in exp;
	int addrlen;
	char *ptr;

	memset(&exp, 0x00, sizeof(struct sockaddr_in));

	yes = TRUE;
	ioctl(fd, FIONBIO, &yes);

	addrlen = sizeof(struct sockaddr_in);
	new = accept(fd, (struct sockaddr *)&exp, &addrlen);

	yes = FALSE;
	ioctl(fd, FIONBIO, &yes);

	if (new < 0) 
	{
		if (errno == EWOULDBLOCK)
			return(-1);	/* It's gone ??? */

		syslog(LOG_ERR, "accept error %m\n");
		return(-1); 
	}

	ptr = "Callsign:";
	write(new, ptr, strlen(ptr));

	pl2 = calloc(sizeof(user_t), 1);
	pl2->fd = new;
	pl2->queue = 0;
	pl2->type = AF_INET;
	pl2->state = WAITCALL;
	pl2->peer = NULL;
	pl2->verbose = FALSE;
	strcpy(pl2->user, "None");

	/* Insert the new connection into the list */
	pl2->next = head;
	head = pl2;

	return new;
}

static int get_tcp_callsign(user_t *pl2, char *buffer, int len)
{
	char *sender;
	char *receiver;
	char application[256];
	int offs;
	char *av[10];
	int ac;
	char dnic[5];
	char rsaddr[11];
	char str[80];
	int ndigi;
	struct full_sockaddr_ax25 exp, rec;
	struct full_sockaddr_rose wp, wpaddr, rosebind, roseconnect;
	struct rose_cause_struct rose_cause;
	int addrlen;
	char *ptr;
	char text[256];
	int i;
	int ok;
	int wp;
	int yes;
	int rsfd;
	user_t *pl3;
	alias_t *a;
	int verbose = FALSE;
	int rslen;

	memset(&wp, 0x00, sizeof(struct full_sockaddr_rose));
	memset(&wpaddr, 0x00, sizeof(struct full_sockaddr_rose));
	memset(&rec, 0x00, sizeof(struct full_sockaddr_ax25));
	memset(&exp, 0x00, sizeof(struct full_sockaddr_ax25));
	memset(&rosebind, 0x00, sizeof(struct full_sockaddr_rose));
	memset(&roseconnect, 0x00, sizeof(struct full_sockaddr_rose));
	memset(&rose_cause, 0x00, sizeof(struct rose_cause_struct));

	memset(application, 0x00, 256 * sizeof(char));
	
	/* Deletes the invalid end of the command string */
	for (offs = len-1 ; offs >= 0 ; offs--)
	{
		if ((isgraph(buffer[offs])) && (buffer[offs] != '^'))
			break;
	}
	buffer[offs+1] = '\0';

	/* extract sender */
	ptr = strchr(buffer, '^');
	if (ptr)
		*ptr++ = '\0';
	else
	{
		end(pl2);
		syslog(LOG_ERR, "ERROR: destination callsign missing\n");
		return(-1);
	}

	/* Deletes the '.' for binary information */
	sender = buffer;
	if (*sender == '.')
		++sender;

	/* Check for an alias */
	for (a = cfg.alias ; a != NULL ; a = a->next)
	{
		if (strcasecmp(a->alias, ptr) == 0)
		{
			strcpy(ptr, a->path);
			verbose = TRUE;
			break;
		}
	}


	if (ax25_aton_entry(sender, exp.fsa_ax25.sax25_call.ax25_call) == -1) 
	{
		end(pl2);
		syslog(LOG_ERR, "ERROR: invalid callsign - %s\n", sender);
		return(-1);
	}

	/* Build AX25 structures */
	exp.fsa_ax25.sax25_family = AF_AX25;
	exp.fsa_ax25.sax25_ndigis = 1;

	/* extract arguments of the destination and digis */
	for (ac = 0; ac < 9 ;)
	{
		while ((*ptr) && (!isgraph(*ptr)))
			++ptr;
		if (*ptr == '\0')
			break;
		av[ac] = ptr;
		while (isgraph(*ptr))
		{
			if ((*ptr == '^') || (*ptr == ','))
				break;
			++ptr;
		}
		if (*ptr)
			*ptr++ = '\0';
		if (ac == 1) 
		{
			if (	(strcasecmp(av[1], "VIA") == 0) ||
				(strcasecmp(av[1], "V") == 0) ||
				(strcasecmp(av[1], cfg.callsign) == 0) ||
				(strcasecmp(av[1], cfg.alt_callsign) == 0))
			{
				verbose = (strcasecmp(av[1], cfg.alt_callsign) == 0);
				continue;
			}
		}
		++ac;
	}
	av[ac] = NULL;

	{
		/* Add the local callsign to the WP */
/*		struct full_sockaddr_rose wp; */

		wp.srose_family = AF_ROSE;
		wp.srose_ndigis = 0;

		rose_aton (cfg.fulladdr, wp.srose_addr.rose_addr);
		wp.srose_call = exp.fsa_ax25.sax25_call;

		wp_update_addr(&wp);
	}

	if (ac == 1)
	{
		/* Check if it is an application */
		if (appli_connection(av[0], application))
		{
			sprintf(str, "CALL_TCP=%s", sender);
			putenv(str);
			new_appli_connection(pl2->fd, TRUE, application);
			end(pl2);
			return(-1);
		}

	}

	ndigi = 0;
	receiver = av[0];

	/* Build ROSE address - Skip rec and 1st digi (node) */
	ndigi++;
	ac--;

	strcpy(rsaddr, cfg.fulladdr);

	ok = FALSE;

	if (ac > 0)
	{
		memset(&dnic, 0, sizeof(dnic));

		rslen = rose_add(av[ndigi]);

		/* Check if dnic */
		if ((rslen == 4) && (ac > 0))
		{
			memcpy(dnic, av[ndigi], 4);
			++ndigi;
			--ac;

			rslen = rose_add(av[ndigi]);
		}

		if (rslen == 6)
		{
			if (*dnic)
				memcpy(rsaddr, dnic, 4);
			memcpy(rsaddr+4, av[ndigi], 6);
			++ndigi;
			--ac;
			ok = TRUE;
		}
	}

	roseconnect.srose_family = rosebind.srose_family = AF_ROSE;
	roseconnect.srose_ndigis = ac;
	rosebind.srose_ndigis    = 0;

	rose_aton (cfg.fulladdr, rosebind.srose_addr.rose_addr);
	rosebind.srose_call = exp.fsa_ax25.sax25_call;

	rose_aton (rsaddr, roseconnect.srose_addr.rose_addr);
	if (ax25_aton_entry(receiver, roseconnect.srose_call.ax25_call) == -1)
	{
		end(pl2);
		return(-1);
	}

	if (roseconnect.srose_ndigis)
	{
		if (ax25_aton_entry(av[ndigi], roseconnect.srose_digis[0].ax25_call) == -1)
		{
			end(pl2);
			return(-1);
		}
	}

	wp = 0;

	if (!ok)
	{
		/* No X25L3 routing. Trying via WP */

		if (wp_search(&rec.fsa_ax25.sax25_call, &wpaddr) == 0)
		{
			roseconnect = wpaddr;
			wp = 1;
		}
		else 
		{
			wp = 2;
		}
	}

verbose = 1;
	
	if (verbose)
	{
		static char *constr[] = { "Connecting", "WP routing", "Trying local" } ;

		sprintf(text, "*** %s %s @ %s", constr[wp], ax25_ntoa(&roseconnect.srose_call), fpac2asc(&roseconnect.srose_addr));
		for (i = 0 ; i < roseconnect.srose_ndigis ; i++)
		{
			sprintf(str, ",%s", ax25_ntoa(&roseconnect.srose_digis[0]));
			strcat(text, str);
		}
		strcat(text, "\r");
		msg(pl2, text);
	}

	/* Start the L3 connection */
	if ((rsfd = socket (AF_ROSE, SOCK_SEQPACKET, 0)) < 0)
	{
		syslog(LOG_ERR, "socket_rs\n");
		end(pl2);
		return(-1);
	}

	addrlen = sizeof(struct full_sockaddr_rose);
	
	if (bind (rsfd, (struct sockaddr *) &rosebind, addrlen) == -1)
	{
		syslog(LOG_ERR, "bind_rs\n");
		close (rsfd);
		end(pl2);
      		return(-1);
	}

	/* Non blocking connection */
	yes = 1;
	ioctl(rsfd, FIONBIO, &yes);

	if (connect (rsfd, (struct sockaddr *) &roseconnect, addrlen) == -1)
	{
		if (errno != EINPROGRESS)
		{
			if (pl2->verbose)
			{
				switch (errno) 
				{
				case ENETUNREACH:
					ioctl(rsfd, SIOCRSGCAUSE, &rose_cause); 
					break;
				case EISCONN:
					rose_cause.cause      = ROSE_NUMBER_BUSY;
					rose_cause.diagnostic = 0x48;
					break;
				case EINVAL:
				default:
					rose_cause.cause      = ROSE_LOCAL_PROCEDURE;
					rose_cause.diagnostic = 0;
					break;
				}
				msg(pl2, "*** Disconnected from %s\r*** %02X%02X - %s\r", 
					ax25_ntoa(&roseconnect.srose_call), 
					rose_cause.cause, 
					rose_cause.diagnostic, 
					reason(rose_cause.cause, rose_cause.diagnostic));
			}
			close (rsfd);
			end(pl2);
			return(-1);
		}
	}
  
	yes = 0;
	ioctl(rsfd, FIONBIO, &yes);

	pl3 = calloc(sizeof(user_t), 1);
	pl3->fd = rsfd;
	pl3->queue = 0;
	pl3->type = AF_ROSE;
	pl3->state = CPROGRESS;
	pl3->peer = pl2;
	pl3->verbose = verbose;
	strcpy(pl3->user, ax25_ntoa(&roseconnect.srose_call));

	/* Insert the new connection into the list */
	pl3->next = head;
	head = pl3;

	/* give L2 the L3 link */
	pl2->peer = pl3;

	return(pl2->fd);
}
#endif

static int rose_add(char *str)
{
	int len = 0;

	while (isdigit(*str))
	{
		++len;
		++str;
	}

	if ((*str) && (*str != '-'))
		return(0);

	return(len);
}

static char *reason(unsigned char cause, unsigned char diagnostic)
{
	static char *desc;

	switch (cause) 
	{
	case ROSE_DTE_ORIGINATED:
		desc = "Remote Station cleared connection";
		break;
	case ROSE_NUMBER_BUSY:
		if (diagnostic == 0x48)
			desc = "Remote station is connecting to you";
		else
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
