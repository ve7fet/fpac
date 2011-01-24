
/*
 *
 * mon_tcp.c (from mheardd.c)
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
/*#include <sys/stat.h>*/
#include <netdb.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
/*#include <fcntl.h>*/
#include <string.h>

#include "config.h"
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

char selport[80];

#define I_MASK 1
#define U_MASK 2
#define S_MASK 4

int mask = U_MASK;

int logging = FALSE;

/* Datagram tcp send*/
int dg;
struct sockaddr_in sg;

/* Datagram ax25 send*/
int dgx;

static int ftype(unsigned char *, int *, int);

static void terminate(int sig)
{
	if (logging) 
	{
		syslog(LOG_INFO, "terminating on SIGTERM\n");
		closelog();
	}

	exit(0);
}

static void n_cpy(int lg, char *dst, char *src)
{
	strncpy(dst, src, lg);
	dst[lg] = '\0';
}

void putax25(int s)
{
	int lg;
	int len;
	int pid;
	int paclen;
	char buf[1024];
	char *pbuf = buf;
	char *ptr = buf;
	char *ptmp;
	char tmp[256];
	char ctl[20];
	unsigned int asize;
	int slen, dlen;
	struct sockaddr sa;
	struct full_sockaddr_ax25 dest;
	struct full_sockaddr_ax25 src;

	int rg;

	asize = sizeof(sa);

	if ((len = recvfrom(s, buf, sizeof(buf), 0, &sa, &asize)) == -1) 
	{
		if (logging) 
		{
			syslog(LOG_ERR, "recv: %m");
			closelog();
		}
		return;
	}
	
	/* port */
	lg = 0;
	pbuf = ptr;
	while (*ptr != '^')
	{
		++ptr;
		++lg;
		--len;
	}
	++ptr;
	--len;

	/* from */
	lg = 0;
	pbuf = ptr;
	while (*ptr != '^')
	{
		++ptr;
		++lg;
		--len;
	}
	n_cpy(lg, tmp, pbuf);
	++ptr;
	--len;

	slen = ax25_aton (tmp, &src);
  
	ptmp = ax25_config_get_addr (selport);
	if (ptmp == NULL)
	{
		return;
	}

	ax25_aton_entry(ptmp, (char *)&src.fsa_digipeater[0]);
	src.fsa_ax25.sax25_ndigis = 1;


	/* to & via */
	lg = 0;
	pbuf = ptr;
	while (*ptr != '^')
	{
		++ptr;
		++lg;
		--len;
	}

	n_cpy(lg, tmp, pbuf);
	++ptr;
	--len;

	if ((dlen = ax25_aton(tmp, &dest)) == -1)
	{
		fprintf(stderr, "mon_tcp: unable to convert callsign '%s'\n", tmp);
		return;
	}



	/* ctrl */
	lg = 0;
	pbuf = ptr;
	while (*ptr != '^')
	{
		++ptr;
		++lg;
		--len;
	}
	n_cpy(lg, ctl, pbuf);
	++ptr;
	--len;

	if (strncmp(ctl, "UI", 2) != 0)
	{
		/* Only UIs ! */
		return;
	}

	/* pid */
	lg = 0;
	pbuf = ptr;
	while (*ptr != '^')
	{
		++ptr;
		++lg;
		--len;
	}
	pid = atoi(pbuf);
	++ptr;
	--len;

	/* len */
	lg = 0;
	pbuf = ptr;
	while (*ptr != '^')
	{
		++ptr;
		++lg;
		--len;
	}
	paclen = atoi(pbuf);
	++ptr;
	--len;

	/* Ax25 send socket */
	if ((rg = socket(AF_AX25, SOCK_DGRAM, 0)) == -1) 
	{
		perror("mon_tcp: socket");
		return;
	}
  
	if (bind(rg, (struct sockaddr *)&src, slen) == -1) 
	{
		perror("mon_tcp: bind");
		close(rg);
		return;
	}
  
	if (sendto(rg, ptr, len, 0, (struct sockaddr *)&dest, dlen) == -1) 
	{
		perror("beacon: sendto");
		close(rg);
		return;
	}
  
	close(rg);
}

void getax25(int s)
{
	unsigned char buffer[1500];
	char *data;
	unsigned int pid;
	char from_call[10];
	char to_call[120];
	char type_pk[80];
	char str[255];
	int type, len;
	int nr, ns, pf;
	int size;
	int val = 0;
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

	if ((*selport) && (strcasecmp(selport, port) != 0))
		return;

	data = (char *)buffer;

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

	if (!ax25_validate((const char *)data + 0) || !ax25_validate((const char *)data + AXLEN)) 
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
			syslog(LOG_WARNING, "packet too short\n");
		return;
	}

	ctlen = ftype((unsigned char *)data, &type, extseq);

	switch (type) 
	{
		case SABM:
			val = (mask & S_MASK);
			strcpy(type_pk, "SABM");
			break;
		case SABME:
			val = (mask & S_MASK);
			strcpy(type_pk, "SABME");
			break;
		case DISC:
			val = (mask & S_MASK);
			strcpy(type_pk, "DISC");
			break;
		case UA:
			val = (mask & S_MASK);
			strcpy(type_pk, "UA");
			break;
		case DM:
			val = (mask & S_MASK);
			strcpy(type_pk, "DM");
			break;	
		case RR:
			val = (mask & S_MASK);
			nr   = (*data >> 5) & 7;
			pf   = *data & PF;
			sprintf(type_pk, "RR%d", nr);
			break;
		case RNR:
			val = (mask & S_MASK);
			nr   = (*data >> 5) & 7;
			pf   = *data & PF;
			sprintf(type_pk, "RNR%d", nr);
			break;
		case REJ:
			val = (mask & S_MASK);
			nr   = (*data >> 5) & 7;
			pf   = *data & PF;
			sprintf(type_pk, "REJ%d", nr);
			break;
		case FRMR:
			val = (mask & S_MASK);
			strcpy(type_pk, "FRMR");
			break;
		case I:
			val = (mask & I_MASK);
			ns = (*data >> 1) & 7;
			nr = (*data >> 5) & 7;
			pf = *data & EPF;
			sprintf(type_pk, "I%d%d", ns, nr);
			break;
		case UI:
			val = (mask & U_MASK);
			strcpy(type_pk, "UI");
			break;
		default:
			strcpy(type_pk, "??");
			break;
	}
	
	data += ctlen;
	size -= ctlen;

	if (type == I || type == UI) 
	{
		pid = (*data++);
		size--;
	}
	else
	{
		pid = 0;
	}
	
	if (!val)
		return;

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
}

int main(int argc, char **argv)
{
	int r;
	int s;
	char *ptr;
	char address[80];
	struct hostent *hp;
	int tcport;
	fd_set read_fdset;
	struct sockaddr_in sc;

	tcport = 23;
	selport[0] = address[0] = '\0';

	while ((s = getopt(argc, argv, "lvp:a:m:")) != -1) 
	{
		switch (s) {
			case 'l':
				logging = TRUE;
				break;
			case 'm':
				strcpy(selport, optarg);
				break;
			case 'a':
				strcpy(address, optarg);
				break;
			case 'o':
				ptr = optarg;
				mask = 0;
				while (*ptr)
				{
					switch (*ptr)
					{
					case 'I':
					case 'i':
						mask |= I_MASK;
						break;
					case 'U':
					case 'u':
						mask |= U_MASK;
						break;
					case 'S':
					case 's':
						mask |= S_MASK;
						break;
					}
					++ptr;
				}
				break;
			case 'p':
				tcport = atoi(optarg);
				break;
			case 'v':
				printf("mon_tcp: %s\n", VERSION);
				return 0;
			case ':':
				fprintf(stderr, "mon_tcp: option needs an argument\n");
				return 1;
			case '?':
				fprintf(stderr, "Usage: mon_tcp [-l] [-a address] [-m port_mon] [-o I|U|S] [-p port] [-v]\n");
				return 1;
		}
	}

	signal(SIGTERM, terminate);

	if (ax25_config_load_ports() == 0) 
	{
		fprintf(stderr, "mon_tcp: no AX.25 port data configured\n");
		return 1;
	}

	/* AX.25 receive socket */
	/*if ((s = socket(AF_INET, SOCK_PACKET, htons(ETH_P_ALL))) == -1) */
	if ((s = socket(AF_PACKET, SOCK_PACKET, htons(ETH_P_ALL))) == -1) 
	{
		perror("mon_tcp: socket ax25");
		syslog(LOG_ERR, "mon_tcp: socket ax25");
		return 1;
	}
	
	/* Ethernet Send socket */
	if ((dg = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
	{
		perror("mon_tcp: socket dgram/s");
		return 1;
	}

	sc.sin_family = AF_INET;
	sc.sin_addr.s_addr = INADDR_ANY;
	sc.sin_port = 0;

	if (bind(dg, (struct sockaddr *)&sc, sizeof(sc)) == -1)
	{
		perror("mon_tcp: socket bind");
		return 1;
	}

	sg.sin_family = AF_INET;

	if ((hp = gethostbyname(address)) == NULL)
	{
		printf("Unknown host %s\n", address);
		return 3;
	}

	sg.sin_addr.s_addr = ((struct in_addr *)(hp->h_addr))->s_addr;
	sg.sin_port = htons(tcport);

	/* Ethernet receive socket */
	if ((r = socket(AF_INET, SOCK_DGRAM, 0)) == -1) 
	{
		perror("mon_tcp: socket dgram/r");
		return 1;
	}
	
	sc.sin_family = AF_INET;
	sc.sin_addr.s_addr = INADDR_ANY;
	sc.sin_port = htons(tcport);

	if (bind(r, (struct sockaddr *)&sc, sizeof(sc)) == -1)
	{
		perror("mon_tcp: socket bind");
		return 1;
	}

	if (!daemon_start(FALSE)) 
	{
		fprintf(stderr, "mon_tcp: cannot become a daemon\n");
		return 1;
	}

	/* Use syslog for error messages rather than perror/fprintf */
	if (logging) 
	{
		openlog("mon_tcp", LOG_PID, LOG_DAEMON);
		syslog(LOG_INFO, "starting");
	}

	for (;;) 
	{

		FD_ZERO(&read_fdset);
		FD_SET(s, &read_fdset);
		FD_SET(r, &read_fdset);

		if (select(r + 1, &read_fdset, 0, 0, 0) == -1)
		{
			printf("select %s\n", strerror(errno));
			break;
		}

		if (FD_ISSET(s, &read_fdset))
		{
			getax25(s);
		}

		if (FD_ISSET(r, &read_fdset))
		{
			putax25(r);
		}

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
