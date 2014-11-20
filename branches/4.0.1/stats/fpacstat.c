
/*
 *
 * fpacstat.c (from mon_ax25 & mheardd.c)
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
#include <sys/param.h>
#include <sys/stat.h>
/*#include <netdb.h>*/

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>

#include "config.h"
#include "../pathnames.h"
#include "ax25compat.h"
#include "fpac.h"

#define	KISS_MASK	0x0F
#define	KISS_DATA	0x00

#define	PID_SEGMENT	0x08
#define	PID_ARP		0xCD
#define	PID_NETROM	0xCF
#define	PID_IP		0xCC
#define	PID_ROSE	0x01
#define	PID_TEXNET	0xC3
#define	PID_FLEXNET	0xCE
#define	PID_TEXT	0xF0
#define	PID_PSATFT	0xBB
#define	PID_PSATPB	0xBD

#define	I		0x00
#define	S		0x01
#define	RR		0x01
#define	RNR		0x05
#define	REJ		0x09
#define	U		0x03
#define	SABM	0x2F
#define	SABME	0x6F
#define	DISC	0x43
#define	DM		0x0F
#define	UA		0x63
#define	FRMR	0x87
#define	UI		0x03

#define	PF		0x10
#define	EPF		0x01

#define	MMASK		7

#define	HDLCAEB		0x01
#define	SSID		0x1E
#define	SSSID_SPARE	0x40
#define	ESSID_SPARE	0x20

#define	ALEN		6
#define	AXLEN		7

#define I_MASK 1
#define U_MASK 2
#define S_MASK 4

/* Period to flush the data to disk */
#define PERIOD 60L
#define HORARY (PERIOD * 60L)

char node_call[10];
int logging = FALSE;
int cr = FALSE;
int hour_request = FALSE;
int min_request = FALSE;
int reset_request = FALSE;
time_t start;

/* Configuration structure */
cfg_t cfg;

typedef struct count
{
	long size;
	long retsize;
	long uisize;
	long sabm;
	long sabme;
	long disc;
	long ua;
	long dm;
	long i;
	long rr;
	long rnr; 
	long rej; 
	long frmr;
	unsigned short nr;
	unsigned short ns;
	unsigned short frm[8];
} count_t;

#define NB_HOUR 24
typedef struct adj
{
	char call[10];
	char *port;
	count_t h_to[NB_HOUR];
	count_t h_fm[NB_HOUR];
	count_t c_to;
	count_t c_fm;
	struct adj *next;
} adj_t;

int current_hour = 0;
adj_t *head;		/* head */

static int ftype(unsigned char *, int *, int);

#define CR(fptr) \
{ \
	if (cr) fputc('\r', fptr); else fputc('\n', fptr); \
}

static void SignalHUP(int code)
{
	reset_request = 1;
}

static void SignalTERM(int code)
{
	unlink(STATPID);
	unlink(STATLCK);
	
	syslog(LOG_INFO, "terminating on SIGTERM\n");
	closelog();
	
	exit(0);
}

static void free_head(adj_t *head)
{
	adj_t *h;

	while (head)
	{
		h = head;
		head = head->next;
		free(h);
	}
}

static adj_t *new_head(void)
{
	int i;
	node_t *n;
	adj_t *head = NULL;
	adj_t *h;
	adj_t *adj;

	for (n = cfg.node, h = head ; n ; n = n->next)
	{
/*		adj_t *adj;	*/
		
		if (n->call[0] == '\0')
			continue;
			
		adj = calloc(1, sizeof(adj_t));
		if (adj == NULL)
			exit(1);
			
		for (i = 0 ; i < 9 ; i++)
		{
			if (!isgraph(n->call[i]))
				break;
				
			adj->call[i] = n->call[i];
		}
		adj->call[i] = '\0';
		
		adj->port = n->port;
		adj->next = NULL;
		
		if (h)
		{
			h->next = adj;
			h = h->next;
		}
		else
		{
			h = head = adj;
		}
	}
	
	return(head);
}

/* Stats resetted - all counters set to 0 */
static void reset_stats(void)
{
	free_head(head);
	head = new_head();

	/* Done */
	reset_request = 0;
}

/* Stats written each hour - last 24 hours information */
static void write_hour_stats(void)
{
	int i;
	long eff;
	adj_t *n;
	char *ptr;
	FILE *fptr;
	time_t t;
	struct tm *tm;
	long round;
	count_t fm, to;

	fptr = fopen(STATDAY, "w");
	if (fptr)
	{
		t = time(NULL) - (time_t)(NB_HOUR * HORARY);
		tm = localtime(&t);
		ptr = asctime(tm);
		ptr[strlen(ptr)-1] = '\0';
		fprintf(fptr,  "Last 24 hours statistics - Starting date : %s", ptr);
		CR(fptr);CR(fptr);
		fprintf(fptr,  "   Adjacent  data-size qual i-frame     rr  rnr  rej sabm disc   ua   dm");
		CR(fptr);

		for (n = head ; n ; n = n->next)
		{
/*			count_t fm, to; */
			
			memset(&fm, 0, sizeof(count_t));
			memset(&to, 0, sizeof(count_t));

			for (i = 0 ; i < NB_HOUR ; i++)
			{
				fm.size    += n->h_fm[i].size;
				fm.retsize += n->h_fm[i].retsize;
				fm.uisize  += n->h_fm[i].uisize;
				fm.i       += n->h_fm[i].i;
				fm.rr      += n->h_fm[i].rr;
				fm.rnr     += n->h_fm[i].rnr;
				fm.rej     += n->h_fm[i].rej;
				fm.sabm    += n->h_fm[i].sabm;
				fm.sabme   += n->h_fm[i].sabme;
				fm.disc    += n->h_fm[i].disc;
				fm.ua      += n->h_fm[i].ua;
				fm.dm      += n->h_fm[i].dm;
				fm.frmr    += n->h_fm[i].frmr;

				to.size    += n->h_to[i].size;
				to.retsize += n->h_to[i].retsize;
				to.uisize  += n->h_to[i].uisize;
				to.i       += n->h_to[i].i;
				to.rr      += n->h_to[i].rr;
				to.rnr     += n->h_to[i].rnr;
				to.rej     += n->h_to[i].rej;
				to.sabm    += n->h_to[i].sabm;
				to.sabme   += n->h_to[i].sabme;
				to.disc    += n->h_to[i].disc;
				to.ua      += n->h_to[i].ua;
				to.dm      += n->h_to[i].dm;
				to.frmr    += n->h_to[i].frmr;
			}
			if (to.size)
			{
				round = to.size / 200L; /* 0.5 % */
				eff = (to.size + round - to.retsize) * 100 / to.size;
			}
			else
				eff = 0;
			fprintf(fptr,  "to %-10s %8ld %3ld%% %7ld %6ld %4ld %4ld %4ld %4ld %4ld %4ld",
							n->call, to.size, eff, to.i, to.rr, to.rnr, 
							to.rej, to.sabm, to.disc, to.ua, to.dm);
			CR(fptr);
			fprintf(fptr,  "fm %-10s %8ld      %7ld %6ld %4ld %4ld %4ld %4ld %4ld %4ld",
							n->call, fm.size, fm.i, fm.rr, fm.rnr, 
							fm.rej, fm.sabm, fm.disc, fm.ua, fm.dm);
			CR(fptr); CR(fptr);
		}
		fclose(fptr);
	}
}

static void new_hour(void)
{
	adj_t *n;

	/* Copy the stats of the last hour */
		
	for (n = head ; n ; n = n->next)
	{
		/* Replace the horary stats of the previous day */
		n->h_fm[current_hour] = n->c_fm;
		n->h_to[current_hour] = n->c_to;
		
		/* Start a new hour */
		memset(&n->c_fm, 0, sizeof(count_t));
		memset(&n->c_to, 0, sizeof(count_t));
	}
	
	/* Date for the current hour statistics */
	start = time(NULL);
	
	/* Count for next hour */
	if (++current_hour == NB_HOUR)
		current_hour = 0;
}

/* Stats written each minute - current hour information */
static void write_min_stats(void)
{
	int fd;
	long eff;
	adj_t *n;
	char *ptr;
	FILE *fptr;
	struct tm *tm;
	long round;

	/* Creates the lock file if it does not exist */
	fd = open(STATLCK, O_RDWR|O_CREAT|O_EXCL, S_IREAD|S_IWRITE);
	if (fd == -1)
	{
		/* Local file exists... Abort update */
		return;
	}
	close(fd);
		
	fptr = fopen(STATDAT, "w");
	if (fptr)
	{
		tm = localtime(&start);
		ptr = asctime(tm);
		ptr[strlen(ptr)-1] = '\0';
		fprintf(fptr,  "Current hour statistics - Starting date : %s", ptr);
		CR(fptr);CR(fptr);
		fprintf(fptr,  "   Adjacent  data-size qual i-frame     rr  rnr  rej sabm disc   ua   dm");
		CR(fptr);

		for (n = head ; n ; n = n->next)
		{
			if (n->c_to.size)
			{
				round = n->c_to.size / 200L; /* 0.5 % */
				eff = (n->c_to.size + round - n->c_to.retsize) * 100 / n->c_to.size;
			}
			else
				eff = 0;
			fprintf(fptr,  "to %-10s %8ld %3ld%% %7ld %6ld %4ld %4ld %4ld %4ld %4ld %4ld",
							n->call, n->c_to.size, eff, 
							n->c_to.i, n->c_to.rr, n->c_to.rnr, 
							n->c_to.rej, n->c_to.sabm, n->c_to.disc, 
							n->c_to.ua, n->c_to.dm);
			CR(fptr);
			fprintf(fptr,  "fm %-10s %8ld      %7ld %6ld %4ld %4ld %4ld %4ld %4ld %4ld",
							n->call, n->c_fm.size, n->c_fm.i, n->c_fm.rr, 
							n->c_fm.rnr, n->c_fm.rej, n->c_fm.sabm, 
							n->c_fm.disc, n->c_fm.ua, n->c_fm.dm);
			CR(fptr); CR(fptr);
		}
		fclose(fptr);
	}
	
	min_request = FALSE;
	
	if (hour_request)
	{
		new_hour();
		write_hour_stats();	
		hour_request = 0;
	}
	
	/* Deletes the lock file */	
	unlink(STATLCK);
}

static int ftype(unsigned char *data, int *type, int extseq)
{
	if (extseq) 
	{
		if ((*data & 0x01) == 0) 
		{	/* An I frame is an I-frame ... */
			*type = I;
			return 2;
		}
		if (*data & 0x02) 
		{
			*type = *data & ~PF;
			return 1;
		} 
		else 
		{
			*type = *data;
			return 2;
		}
	} 
	else 
	{
		if ((*data & 0x01) == 0) 
		{	/* An I frame is an I-frame ... */
			*type = I;
			return 1;
		}
		if (*data & 0x02) 
		{	/* U-frames use all except P/F bit for type */
			*type = *data & ~PF;
			return 1;
		} 
		else 
		{
			/* S-frames use low order 4 bits for type */
			*type = *data & 0x0F;
			return 1;
		}
	}
}

/* Mark acknowledged frames */
static void mark_ok(count_t *to, count_t *fm, unsigned short tst)
{
	unsigned short nr = fm->nr;

	while (nr != tst) {
		to->frm[nr] = 0;
		nr = (nr + 1) % 8;
	}
	return;
}

static int node(char *port, char *fm_call, char *to_call, count_t **fm, count_t **to)
{
	adj_t *n;
	
	*fm = *to = NULL;
	
	for (n = head ; n ; n = n->next)
	{
		if ((strcmp(n->call, fm_call) == 0) && (strcmp(cfg.callsign, to_call) == 0) && (strcmp(port, n->port) == 0))
		{
			*fm = &n->c_fm;
			*to = &n->c_to;
			return TRUE;
		}
		if ((strcmp(n->call, to_call) == 0) && (strcmp(cfg.callsign, fm_call) == 0) && (strcmp(port, n->port) == 0))
		{
			*fm = &n->c_to;
			*to = &n->c_fm;
			return TRUE;
		}
	}
	return FALSE;
}

void getax25(int s)
{
	unsigned char buffer[1500];
	unsigned char *data;
	unsigned int pid;
	char from_call[10];
	char to_call[120];
	char type_pk[80];
	int type;
	int nr;
	int pf;
	int size;
	char *port = NULL;
	char *ptr;
	unsigned int asize;
	int ctlen, end, extseq;
	struct sockaddr sa;
	count_t *fm, *to;


	asize = sizeof(sa);

	if ((size = recvfrom(s, buffer, sizeof(buffer), 0, &sa, &asize)) == -1) 
	{
		if (logging) 
		{
			syslog(LOG_ERR, "recv: %m");
			closelog();
		}
		return;
	}
	
	if ((port = ax25_config_get_name(sa.sa_data)) == NULL) 
	{
		if (logging)
			syslog(LOG_WARNING, "unknown port '%s'\n", sa.sa_data);
		return;
	}

	data = buffer;

	if ((*data & KISS_MASK) != KISS_DATA)
		return;

	data++;
	size--;

	if (size < (AXLEN + AXLEN + 1)) 
	{
		if (logging)
			syslog(LOG_WARNING, "packet too short\n");
		return;
	}

	if (!ax25_validate((char *)data + 0) || !ax25_validate((char *)data + AXLEN)) 
	{
		if (logging)
			syslog(LOG_WARNING, "invalid callsign on port %s\n", port);
		return;
	}

	strcpy(from_call, ax25_ntoa((ax25_address *)(data + AXLEN)));
	ptr = strstr(from_call, "-0");
	if (ptr)
		*ptr = '\0';

	strcpy(to_call,   ax25_ntoa((ax25_address *)data));
	ptr = strstr(to_call, "-0");
	if (ptr)
		*ptr = '\0';

	/* Check if the packet is a packet to an adjacent */
	if (!node(port, from_call, to_call, &fm, &to))
		return;
	
	extseq = ((data[AXLEN + ALEN] & SSSID_SPARE) != SSSID_SPARE);
	end    = (data[AXLEN + ALEN] & HDLCAEB);

	data += (AXLEN + AXLEN);
	size -= (AXLEN + AXLEN);

	while (!end) 
	{
		strcat(to_call, " ");
		strcat(to_call, ax25_ntoa((ax25_address *)data));
		ptr = strstr(to_call, "-0");
		if (ptr)
			*ptr = '\0';

		end = (data[ALEN] & HDLCAEB);
		
		data += AXLEN;
		size -= AXLEN;
	}

	if (size == 0) 
	{
		if (logging)
			syslog(LOG_WARNING, "packet too short\n");
		return;
	}

	ctlen = ftype(data, &type, extseq);
	size -= ctlen;

	switch (type) 
	{
		case SABM:
			++fm->sabm;
			strcpy(type_pk, "SABM");
			break;
		case SABME:
			++fm->sabme;
			strcpy(type_pk, "SABME");
			break;
		case DISC:
			++fm->disc;
			strcpy(type_pk, "DISC");
			break;
		case UA:
			++fm->ua;
			strcpy(type_pk, "UA");
			break;
		case DM:
			++fm->dm;
			strcpy(type_pk, "DM");
			break;	
		case RR:
			++fm->rr;
			nr = (*data >> 5) & 7;
			mark_ok(to, fm, nr);
			fm->nr = nr;
			pf   = *data & PF;
			sprintf(type_pk, "RR%d", fm->nr);
			break;
		case RNR:
			++fm->rnr;
			nr = (*data >> 5) & 7;
			mark_ok(to, fm, nr);
			fm->nr = nr;
			pf   = *data & PF;
			sprintf(type_pk, "RNR%d", fm->nr);
			break;
		case REJ:
			++fm->rej;
			nr = (*data >> 5) & 7;
			mark_ok(to, fm, nr);
			fm->nr = nr;
			pf = *data & PF;
			sprintf(type_pk, "REJ%d", fm->nr);
			break;
		case FRMR:
			++fm->frmr;
			strcpy(type_pk, "FRMR");
			break;
		case I:
			++fm->i;
			fm->ns = (*data >> 1) & 7;
			nr = (*data >> 5) & 7;
			mark_ok(to, fm, nr);
			fm->nr = nr;
			pf = *data & EPF;
			fm->size += (size-1);
			if (fm->frm[fm->ns] == 1)	/* frame already sent */
				fm->retsize += (size-1);
			fm->frm[fm->ns] = 1;	/* frame sent */
			sprintf(type_pk, "I%d%d", fm->ns, fm->nr);
			break;
		case UI:
			strcpy(type_pk, "UI");
			fm->uisize += size;
			break;
		default:
			strcpy(type_pk, "??");
			break;
	}
	
	data += ctlen;

	if (type == I || type == UI) 
	{
		pid = (*data++);
		size--;
	}
	else
	{
		pid = 0;
	}
	
	/* 
	sprintf(str, "%s^%s^%s^%s^%02x^%d^", port, from_call, to_call, type_pk, pid, size);
	printf("%s\n", str);

	if (size > 0)
	{
		data[size] = '\0';
		printf("%s\n", data);
	}
	*/
}

int main(int argc, char **argv)
{
	int s;
	int nb;
	int nb_req;
	fd_set read_fdset;
	time_t date, temps;
	struct timeval timeval;
	FILE *fptr;
	struct sigaction act, oact;
	
	while ((s = getopt(argc, argv, "lvc")) != -1) 
	{
		switch (s) {
			case 'c':
				cr = TRUE;
				break;
			case 'l':
				logging = TRUE;
				break;
			case 'v':
				printf("fpacstat: %s\n", VERSION);
				return 0;
			case ':':
				fprintf(stderr, "fpacstat: option needs an argument\n");
				return 1;
			case '?':
				fprintf(stderr, "Usage: fpacstat [-l] [-a address] [-m port_mon] [-o I|U|S] [-p port] [-v]\n");
				return 1;
		}
	}

	if (ax25_config_load_ports() == 0) 
	{
		fprintf(stderr, "fpacstat: no AX.25 port data configured\n");
		return 1;
	}

	/* Open the configuration file */
	if (cfg_open(&cfg) != 0)
	{
		return(1);
	}

	head = new_head();
	
	/* AX.25 receive socket */
	/*if ((s = socket(AF_INET, SOCK_PACKET, htons(ETH_P_ALL))) == -1) */
	if ((s = socket(AF_PACKET, SOCK_PACKET, htons(ETH_P_ALL))) == -1) 
	{
		perror("fpacstat: socket ax25");
		syslog(LOG_ERR, "fpacstat: socket ax25");
		return 1;
	}

	if (!daemon_start(FALSE)) 
	{
		fprintf(stderr, "fpacstat: cannot become a daemon\n");
		syslog(LOG_ERR, "fpacstat: cannot become a daemon");
		return 1;
	}

	/* initialize signal handlers */ 
	act.sa_handler = SignalHUP;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGHUP, &act, &oact);

	act.sa_handler = SignalTERM;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGTERM, &act, &oact);
	
	/* Use syslog for error messages rather than perror/fprintf */
	if (logging) 
	{
		openlog("fpacstat", LOG_PID, LOG_DAEMON);
		syslog(LOG_INFO, "starting");
	}

	fptr = fopen(STATPID, "w");
	if (fptr)
	{
		fprintf(fptr, "%d\n", getpid());
		fclose(fptr);
	}
	
	start = date = time(NULL);
	nb_req  = (HORARY / PERIOD);
	
	for (;;) 
	{

		FD_ZERO(&read_fdset);
		FD_SET(s, &read_fdset);

		timeval.tv_usec = 0;
		timeval.tv_sec  = 1;

		nb = select(s + 1, &read_fdset, NULL, NULL, &timeval);
		if (nb == -1)
		{
			if (errno != EINTR)
			{
				printf("select %s\n", strerror(errno));
				break;
			}
		}

		if (reset_request)
		{
			reset_stats();
			start = date = time(NULL);
			nb_req  = (HORARY / PERIOD);
		}
		
		if (nb && (FD_ISSET(s, &read_fdset)))
		{
			getax25(s);
		}

		temps = time(NULL);
		if (date <= temps)
		{
			min_request = TRUE;
			date += PERIOD;
			if (nb_req == (HORARY / PERIOD))
			{
				nb_req = 0;
				hour_request = TRUE;
			}
			else
				++nb_req;
		}		
		
		if (min_request)
			write_min_stats();
		
	}
	return 0;
}
