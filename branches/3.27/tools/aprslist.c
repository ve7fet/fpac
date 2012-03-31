/*
* aprslist.c (after mheardd.c)
* This creates the aprslist binary
*
* It uses the aprslist.conf file to beacon a
* aprs position (and beacon) packet out a defined
* port in axports
*
* It also listens to the port and logs aprs packets
* to aprs.dat
*
* Options:
*      -d don't fork in to daemon mode
*      -l log to syslog
*      -s show the contents of aprs.dat and exit
*      -v show version
*
*/

#include <math.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include <linux/if_ether.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/stat.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>

#include "config.h"
#include "../pathnames.h"
#include "ax25compat.h"

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
#define	SABM		0x2F
#define	SABME		0x6F
#define	DISC		0x43
#define	DM		0x0F
#define	UA		0x63
#define	FRMR		0x87
#define	UI		0x03

#define	PF		0x10
#define	EPF		0x01

#define	MMASK		7

#define	HDLCAEB		0x01
#define	REPEATED	0x80
#define	SSID		0x1E
#define	SSSID_SPARE	0x40
#define	ESSID_SPARE	0x20

#define	ALEN		6
#define	AXLEN		7
#define	BEACON_TEXT_BUFFER	123

#define n_cpy(a, b, c) strncpy(a, b, c)

char selport[80];

#define I_MASK 1
#define U_MASK 2
#define S_MASK 4

typedef struct aprsl_ {
	char call[10];
	char loc[20];
	time_t date;
	time_t last;
	struct aprsl_ *next;
} aprsl;

aprsl *head = NULL;
aprsl *tail;

typedef struct sport_ {
	char name[10];
	char from[10];
	char to[80];
	char *test;
	int period;
	time_t time;
	struct sport_ *next;
} sport;

sport *phead = NULL;

double my_w = 0.0;
double my_l = 0.0;
char my_loc[22];
char beacon[BEACON_TEXT_BUFFER];

int mask = U_MASK;

int logging = FALSE;

/* Datagram tcp send*/
int dg;
struct sockaddr_in sg;

/* Datagram ax25 send*/
int dgx;

static int ftype(unsigned char *, int *, int);
static void SignalTERM(int);

static void SignalTERM(int code)
{
	syslog(LOG_INFO, "terminating on SIGTERM\n");
	closelog();
	
	exit(0);
}

/*static void n_cpy(char *dst, char *src, int lg)
{
	n_cpy(dst, src, lg);
	dst[lg] = '\0';
}
*/

int send_ax25(sport *ps)
{
	int len;
	char buf[1024];
	char *ptr = buf;
	char *ptmp;
	int slen, dlen;
	struct full_sockaddr_ax25 dest;
	struct full_sockaddr_ax25 src;

	int rg;

	slen = ax25_aton (ps->from, &src);

	sprintf(buf, "%s#%s\r", my_loc, beacon);
	len = strlen(buf);
  
	ptmp = ax25_config_get_addr (ps->name);
	if (ptmp == NULL)
	{
		fprintf(stderr, "aprslist: invalid port name '%s' in %s\n", ps->name, CONF_APRSLIST_FILE);
		return 1;
	}

	ax25_aton_entry(ptmp, (char *)&src.fsa_digipeater[0]);
	src.fsa_ax25.sax25_ndigis = 1;


	if ((dlen = ax25_aton(ps->to, &dest)) == -1)
	{
		fprintf(stderr, "aprslist: unable to convert callsign '%s'\n", ps->to);
		return 1;
	}

	/* Ax25 send socket */
	if ((rg = socket(AF_AX25, SOCK_DGRAM, 0)) == -1) 
	{
		perror("aprslist: socket");
		return 1;
	}
  
	if (bind(rg, (struct sockaddr *)&src, slen) == -1) 
	{
		perror("aprslist: bind");
		close(rg);
		return 1;
	}
  
	if (sendto(rg, ptr, len, 0, (struct sockaddr *)&dest, dlen) == -1) 
	{
		perror("aprslist: beacon sendto");
		close(rg);
		return 1;
	}
  
	close(rg);
return 0;
}

#define DEGRAD  57.2957795
double dist (double w, double l, double wa, double la)
{
	double x1, y1;

	y1 = cos (l / DEGRAD) * cos (la / DEGRAD) * cos ((w - wa) / DEGRAD) +
		sin (l / DEGRAD) * sin (la / DEGRAD);
	if (y1 > 1)
		y1 = 1;
	if (y1 < -1)
		y1 = -1;
	x1 = atan (sqrt (1 - y1 * y1) / y1);
	if (y1 < 0)
		x1 = 180 / DEGRAD + x1;
	return (6371.3 * x1);
}

double azimut (double w, double l, double wa, double la)
{
	double x1, y1, z1;

	x1 = dist (w, l, wa, la) / 6371.3;
	if (x1)
	{
		y1 = (sin (la / DEGRAD) - sin (l / DEGRAD) * cos (x1)) / (cos (l / DEGRAD) * sin (x1));
		if (y1 > 1)
			y1 = 1;
		if (y1 < -1)
			y1 = -1;
		z1 = fabs (DEGRAD * atan (sqrt (1 - y1 * y1) / y1));
		if (y1 < 0)
			z1 = 180 - z1;
		if (sin ((w - wa) / DEGRAD) < 0)
			z1 = 360 - z1;
		return (z1);
	}
	else
		return (0);
}

double get_val(char *ptr, int start, int len)
{
	char str[80];
	
	memcpy(str, ptr+start, len);
	str[len] = '\0';
	return (double)atoi(str);
}

aprsl *find_aprs(char *call)
{
	aprsl *ptr = head;
	
	while (ptr)
	{
		if (strcasecmp(call, ptr->call) == 0)
			return ptr;
		ptr = ptr->next;
	}
	
	return NULL;
}
	
void add_aprs(char *call, char *loc)
{
	aprsl *ptr;
	time_t temps;
	
	temps = time(NULL);

	ptr = find_aprs(call);
	if (ptr)
	{
		n_cpy(ptr->loc, loc, 18);
		ptr->last = temps;
		return;
	}
	
	ptr = (aprsl *)calloc(sizeof(aprsl), 1);
	if (ptr == NULL)
		return;
		
	n_cpy(ptr->call, call, 9);
	n_cpy(ptr->loc, loc, 18);
	ptr->last = ptr->date = temps;
		
	if (head)
	{
		tail->next = ptr;
		tail = ptr;
	}
	else
	{
		tail = head = ptr;
	}
}

void display_aprs(aprsl *aprs)
{
	double longitude;
	double lattitude;
	double minutes;
	double seconds;
	char *ptr = aprs->loc;

	lattitude = get_val(ptr, 0, 2);
	minutes   = get_val(ptr, 2, 2);
	seconds   = get_val(ptr, 5, 2);
	lattitude += minutes / 60.0;
	lattitude += seconds / 3600.0;
	longitude = get_val(ptr, 9, 3);
	minutes   = get_val(ptr, 12, 2);
	seconds   = get_val(ptr, 15, 2);
	longitude += minutes / 60.0;
	longitude += seconds / 3600.0;
	if (ptr[7] == 'S')
		lattitude = -lattitude;
	if (ptr[17] == 'E')
		longitude = -longitude;
	printf("%9s : %c%c.%c%c%c%c%c %c%c%c.%c%c%c%c%c km:%-4d az:%-3d %s", 
			aprs->call, 
			ptr[0], ptr[1], ptr[2], ptr[3], ptr[5], ptr[6], ptr[7],
			ptr[9], ptr[10], ptr[11], ptr[12], ptr[13], ptr[15], ptr[16], ptr[17],
			(int)(0.5 + dist(my_w, my_l, longitude, lattitude)),
			(int)(0.5 + azimut(my_w, my_l, longitude, lattitude)),
			ctime(&aprs->last));
}

void read_aprs(void)
{
	FILE *fptr;
	aprsl *ptr;
	char buf[80];
		
	fptr = fopen( DATA_APRSLIST_FILE , "r"); 
	if (fptr == NULL)
		return;

	while (fgets(buf, sizeof buf, fptr))
	{		
		ptr = (aprsl *)calloc(sizeof(aprsl), 1);
		if (ptr == NULL)
			return;

		sscanf(buf, "%s %s %ld %ld\n",
			ptr->call, ptr->loc, &ptr->date, &ptr->last);
		display_aprs(ptr);
		
		if (head)
		{
			tail->next = ptr;
			tail = ptr;
		}
		else
		{
			tail = head = ptr;
		}
	}
	fclose(fptr);
}

void write_aprs(void)
{
	FILE *fptr;
	aprsl *ptr = head;
		
	fptr = fopen( DATA_APRSLIST_FILE, "w");
	if (fptr == NULL)
		return;
		
	while (ptr)
	{
		fprintf(fptr, "%s %s %ld %ld\n",
			ptr->call, ptr->loc, ptr->date, ptr->last);
		ptr = ptr->next;
	}
	fclose(fptr);
}

void dump_aprs(void)
{
	aprsl *ptr = head;
	
	while (ptr)
	{
		display_aprs(ptr);
		ptr = ptr->next;
	}
}
	
int set_position(char *data)
{
	double minutes;
	double seconds;

	if (strlen(data) != 18)
		return 0;
		
	if (data[7] != 'S' && data[7] != 'N')
		return 0;
	if (data[17] != 'E' && data[17] != 'W')
		return 0;

	my_l = get_val(data, 0, 2);
	minutes   = get_val(data, 2, 2);
	seconds   = get_val(data, 5, 2);
	my_l += minutes / 60.0;
	my_l += seconds / 3600.0;
	
	my_w = get_val(data, 9, 3);
	minutes   = get_val(data, 12, 2);
	seconds   = get_val(data, 15, 2);
	my_w += minutes / 60.0;
	my_w += seconds / 3600.0;
	if (data[7] == 'S')
		my_l = -my_l;
	if (data[17] == 'E')
		my_w = -my_w;
	return 1;
}

void decode_aprs(char *from_call, char *data, int size)
{
	int code = *data++;
	
	switch (code) {
	case '!':
	case '=':
		if (data[7] != 'S' && data[7] != 'N')
			break;
		if (data[17] != 'E' && data[17] != 'W')
			break;
		add_aprs(from_call, data);
		printf("\n");
		dump_aprs();
		write_aprs();
		break;
	}
}

void getax25(int s)
{
	unsigned char buffer[1500];
	unsigned char *data;
	unsigned int pid;
	char from_call[10];
	char to_call[120];
	int type;
	int size;
	char *port = NULL;
	char *ptr;
	unsigned int asize;
	int ctlen, end, extseq, rep;
	struct sockaddr sa;

	asize = sizeof(sa);

	if ((size = recvfrom(s, buffer, sizeof(buffer), 0, &sa, &asize)) == -1) 
	{
		if (logging) 
		{
			syslog(LOG_ERR, "aprslist: recvfrom %m");
			closelog();
		}
		return;
	}
	
	if ((port = ax25_config_get_name(sa.sa_data)) == NULL) 
	{
/*		if (logging)
			syslog(LOG_WARNING, "aprslist: unknown port name '%s'\n", sa.sa_data); */
		return;
	}

	if ((*selport) && (strcasecmp(selport, port) != 0))
		return;

	data = buffer;

	if ((*data & KISS_MASK) != KISS_DATA)
		return;

	data++;
	size--;

	if (size < (AXLEN + AXLEN + 1)) 
	{
		if (logging)
			syslog(LOG_WARNING, "aprslist: packet too short\n");
		return;
	}

	if (!ax25_validate((char *)data + 0) || !ax25_validate((char *)data + AXLEN)) 
	{
		if (logging)
			syslog(LOG_WARNING, "aprslist: invalid callsign on port %s\n", port);
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

	extseq = ((data[AXLEN + ALEN] & SSSID_SPARE) != SSSID_SPARE);
	end    = (data[AXLEN + ALEN] & HDLCAEB);
	rep = 0;
	
	data += (AXLEN + AXLEN);
	size -= (AXLEN + AXLEN);

	while (!end) 
	{
		if (rep && (data[ALEN] & REPEATED) == 0)
			strcat(to_call, "*");
		strcat(to_call, " ");
		strcat(to_call, ax25_ntoa((ax25_address *)data));
		ptr = strstr(to_call, "-0");
		if (ptr)
			*ptr = '\0';

		rep = (data[ALEN] & REPEATED);
		end = (data[ALEN] & HDLCAEB);
		data += AXLEN;
		size -= AXLEN;
	}

	if (rep)
		strcat(to_call, "*");
		
	if (size <= 0) 
	{
		if (logging)
			syslog(LOG_WARNING, "apsrlist: packet too short\n");
		return;
	}

	ctlen = ftype(data, &type, extseq);

	data += ctlen;
	size -= ctlen;

	if (type == UI) 
	{
		pid = (*data++);
		size--;
		data[size] = '\0';
		/*
		printf("%s:%s>%s (%d)\n", port, from_call, to_call, size);
		printf("%s\n", data);
		*/
		
		if (size >= 19)
			decode_aprs(from_call, (char *)data, size);
	}
	
	/*	
	sprintf(str, "%s^%s^%s^%s^%02x^%d^", port, from_call, to_call, type_pk, pid, size);
	len = strlen(str);
	if (size > 0)
	{
		memcpy(str+len, data, size);
		len += size;
	}

	if (sendto(dg, str, len, 0, (struct sockaddr *)&sg, sizeof(sg)) == -1)
	{
		if (logging)
			syslog(LOG_WARNING, "sendto: %s\n", strerror(errno));
	}
	*/
}

char *get_next_arg(char **p)
{
	char *p2;
	
	if (p == NULL || *p == NULL)
		return NULL;

	p2 = *p;
	for (; *p2 && *p2 == ' '; p2++) ;
	if (!*p2)
		return NULL;
		
	*p = strchr(p2, ' ');
	if (*p != NULL)
	{
		**p = '\0';
		(*p)++;
	}
	
	return p2;
}

char *prepare_cmdline(char *buf)
{
	char *p;
	for (p = buf; *p; p++)
	{
		if (*p == '\t') *p = ' ';
		*p = tolower(*p);
		
		if (*p == '\n')
		{
			*p = '\0';
			break;
		}
		
		if (*p == '#') 
		{
			*p = '\0';
			break;
		}
	}
	
	return buf;
}

void load_config(void)
{
	FILE *fp;
	char buf[1024], *p, *cmd, *arg;
	char slon[80], slat[80];
	double minutes;
	double seconds;
	char geo;
	sport *ps = NULL;


	fp = fopen(CONF_APRSLIST_FILE, "r");
	
	if (fp == NULL)
	{
		fprintf(stderr, "aprslist: config file %s not found\n", CONF_APRSLIST_FILE);
		exit(1);
	}
	
	while(fgets(buf, sizeof(buf)-1, fp) != NULL)
	{
		p = prepare_cmdline(buf);
		if (!*p)
			continue;

		cmd = get_next_arg(&p);
		if (cmd == NULL)
			continue;

		if (!strncmp(cmd, "lon", 3))
		{
			my_w = atof(get_next_arg(&p));
			minutes = atof(get_next_arg(&p));
			seconds = atof(get_next_arg(&p));
			geo = toupper(*get_next_arg(&p));
			
			sprintf(slon, "%03g%02g.%02g%c", my_w, minutes, seconds, geo);
			slon[9] = '\0';

			my_w += minutes / 60.0;
			my_w += seconds / 3600.0;
			if (geo == 'E')
				my_w = -my_w;
		}
		else if (!strncmp(cmd, "lat", 3))
		{
			my_l = atof(get_next_arg(&p));
			minutes = atof(get_next_arg(&p));
			seconds = atof(get_next_arg(&p));
			geo = toupper(*get_next_arg(&p));
			
			sprintf(slat, "%02g%02g.%02g%c", my_l, minutes, seconds, geo);
			slat[8] = '\0';

			my_l += minutes / 60.0;
			my_l += seconds / 3600.0;
			if (geo == 'S')
				my_l = -my_l;
		}
		else if (*cmd == '[')
		{
			/* Port information */
			cmd++;
			p = strrchr(cmd, ']');
			if (p == NULL)
			{
				fprintf(stderr, "aprslist: syntax error [%s\n", cmd);
				continue;
			}
			*p = '\0';
		
			ps = (sport *)calloc(sizeof(sport), 1);
			strcpy(ps->name, cmd);
			strcpy(ps->to, "aprs");
			strcpy(ps->from, "nocall");
			ps->next = phead;
			phead = ps;
		}
		else if (ps && !strncmp(cmd, "fro", 3))
		{
			arg = get_next_arg(&p);
			n_cpy(ps->from, arg, 9);
		}
		else if (ps && !strncmp(cmd, "to", 2))
		{
			n_cpy(ps->to, p, 79);
		}
		else if (ps && !strncmp(cmd, "per", 3))
		{
			arg = get_next_arg(&p);
			ps->period = atoi(arg) * 60;
		}
		else if (ps && !strncmp(cmd, "bea", 3))
		{ 
			strncpy( beacon, p, BEACON_TEXT_BUFFER);
			beacon[BEACON_TEXT_BUFFER] = '\0';
		}

	}
	
	fclose(fp);
	
	sprintf(my_loc, "=%s/%s", slat, slon);
	printf("%s\n%s\n", my_loc, beacon);
}

int send_beacon(void)
{
	int rc = 0;
	sport *ps;
	time_t t = time(NULL);
	
	for (ps = phead ; ps ; ps = ps->next)
	{
		if (ps->time <= t)
		{
			ps->time = t + (time_t)ps->period;
			rc = send_ax25(ps);
		}
	}
	return rc;
}

int main(int argc, char **argv)
{
	int rc = 0;
	int s;
	int daemon = 1;
	int show = FALSE;
	fd_set read_fdset;
	struct sigaction act, oact;
	
	*beacon = '\0'; 

	while ((s = getopt(argc, argv, "lvds")) != -1) 
	{
		switch (s) {
			case 'l':
				logging = TRUE;
				break;
			case 's':
				show = TRUE;
				break;
			case 'd':
				daemon = 0;
				break;
			case 'v':
				printf("aprslist: %s\n", VERSION);
				return 0;
			case '?':
				fprintf(stderr, "Usage: aprslist [-l] [-s] [-d] [-v]\n");
				return 1;
		}
	}
	
	load_config();

	if (show)
	{
		read_aprs();
		dump_aprs();
		exit(0);
	}
	
	act.sa_handler = SignalTERM;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGTERM, &act, &oact);

	if (ax25_config_load_ports() == 0) 
	{
		fprintf(stderr, "aprslist: no AX.25 port data configured\n");
		return 1;
	}

	/* AX.25 receive socket */
	if ((s = socket(AF_PACKET, SOCK_PACKET, htons(ETH_P_ALL))) == -1)
	{
		syslog(LOG_ERR, "aprslist: socket ax25");
		perror("aprslist: socket ax25");
		return 1;
	}
	
	if (daemon && !daemon_start(FALSE)) 
	{
		fprintf(stderr, "aprslist: cannot become a daemon\n");
		return 1;
	}

	/* Use syslog for error messages rather than perror/fprintf */
	if (logging) 
	{
		openlog("aprslist", LOG_PID, LOG_DAEMON);
		syslog(LOG_INFO, "starting");
	}

	for (;;) 
	{
		struct timeval to;

		to.tv_usec = 0;
		to.tv_sec  = 1;

		FD_ZERO(&read_fdset);
		FD_SET(s, &read_fdset);

		if (select(s+1, &read_fdset, 0, 0, &to) == -1)
		{
			printf("aprslist: select %s\n", strerror(errno));
			break;
		}

		if ((rc = send_beacon()) == 1)
			exit (1);
		
		if (FD_ISSET(s, &read_fdset))
		{
			getax25(s);
		}
	usleep (50000);
	}

	return 0;
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
