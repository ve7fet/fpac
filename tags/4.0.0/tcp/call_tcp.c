#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <sys/file.h>
/*#include <sys/stat.h>*/
#include <sys/time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
/*#include <sys/socket.h>*/
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/ip_icmp.h>
#include <netinet/ip.h>

#include "ax25compat.h"
#include "wp.h"
#include "fpac.h"

#define BUFLEN 4096

void cr2lf(char *str, int len)
{
	while (len--)
	{
		if (*str == '\r')
			*str = '\n';
		++str;
	}
}

void lf2cr(char *str, int len)
{
	while (len--)
	{
		if (*str == '\n')
			*str = '\r';
		++str;
	}
}

int main(int argc, char **argv)
{
	int n;
	int fd;
	int nb;
	int len;
	int yes;
	int type;
	int first;
	int buflen;
	unsigned int addrlen;
	fd_set read_fdset;
	struct hostent *hp;
	struct servent *sp;
	struct sockaddr_in addr;
	char *ptr;
	char buf[4096];
	char caller[120];
	struct full_sockaddr_rose wp;
	cfg_t cfg;
	char address[11];
	union {
		struct full_sockaddr_ax25 sax;
		struct full_sockaddr_rose      srose;
		struct sockaddr_in        sin;
	} saddr;
	unsigned int slen = sizeof(saddr);


	if (argc < 2)
	{
		printf("IP address missing - Usage call_tcp [IP address] [port]\n");
		return 1;
	}
	if (argc < 3)
	{
		printf("Port missing - Usage call_tcp [IP address] [port]\n");
		return 1;
	}


	if (cfg_open(&cfg) != 0)
		return(1);

	strcpy(address, cfg.dnic);
	strcat(address, cfg.address);

	/* Identify the caller */
	if (getpeername(STDOUT_FILENO, (struct sockaddr *)&saddr, &slen) == -1)
	{
		if (errno != ENOTSOCK)
		{
			printf("getpeername: %s", strerror(errno));
			return 1;
		}
		type = AF_UNSPEC;
	} 
	else
		type = saddr.sax.fsa_ax25.sax25_family;

	caller[0] = '.';

	switch (type) 
	{
	case AF_AX25:
		strcpy(caller+1, ax25_ntoa(&saddr.sax.fsa_ax25.sax25_call));
		wp.srose_family = AF_ROSE;
		wp.srose_call   = saddr.sax.fsa_ax25.sax25_call;
		wp.srose_ndigis = saddr.sax.fsa_ax25.sax25_ndigis;
		if (wp.srose_ndigis > ROSE_MTU)
			wp.srose_ndigis = ROSE_MTU;
		for (n = 0 ; n < wp.srose_ndigis ; n++)
			wp.srose_digis[n]   = saddr.sax.fsa_digipeater[n];
		rose_aton(address, wp.srose_addr.rose_addr); 
		wp_open("CALTCP");
		wp_update_addr(&wp);
		wp_close();
		break;
	case AF_NETROM:
		strcpy(caller+1, ax25_ntoa(&saddr.sax.fsa_ax25.sax25_call));
		break;
	case AF_ROSE:
		/* Accept the connection */
		yes = 1;
		ioctl(STDOUT_FILENO, SIOCRSACCEPT, &yes);
		strcpy(caller+1, ax25_ntoa(&saddr.srose.srose_call));
		wp = saddr.srose;
		wp_open("CALTCP");
		wp_update_addr(&wp);
		wp_close();
		break;
	case AF_INET:
		ptr = getenv("CALL_TCP");
		if (ptr)
		{
			strcpy(caller+1, ptr);
		}
		else
		{
			printf("Variable CALL_TCP missing\r");
			return 3;
		}
		break;
	case AF_UNSPEC:
		printf("Enter your callsign :");
		fgets(caller, sizeof(caller)-1, stdin);
		caller[7] = '\0';
		break;
	default:
		printf("Unsupported address family %d\n", type);
		return 1;
	}

	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) 
	{
		printf("socket: %s\n", strerror(errno));
		return 2;
	}

	buflen = BUFLEN;
	setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buflen, sizeof(buflen));
	setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buflen, sizeof(buflen));

	if ((hp = gethostbyname(argv[1])) == NULL)
	{
		printf("Unknown host %s\n", argv[1]);
		close(fd);
		return 3;
	}

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = ((struct in_addr *)(hp->h_addr))->s_addr;

	sp = NULL;
	if (argc < 3)
		sp = getservbyname("telnet", "tcp");
	if (sp == NULL)
		sp = getservbyname(argv[2], "tcp");
	if (sp == NULL)
		sp = getservbyport(htons(atoi(argv[2])), "tcp");
	if (sp != NULL) 
	{
		addr.sin_port = sp->s_port;
	}
	else if (atoi(argv[2]) != 0) 
	{
		addr.sin_port = htons(atoi(argv[2]));
	} 
	else 
	{
		printf("Unknown service %s\n", argv[2]);
		close(fd);
		return 4;
	}

	addrlen = sizeof(addr);

	if (connect(fd, (struct sockaddr *)&addr, addrlen) == -1 && errno != EINPROGRESS) 
	{
		printf("connect: %s\n", strerror(errno));
		close(fd);
		return -1;
	}

	{
		union {
			unsigned long l;
			unsigned char b[4];
		} add;
		add.l = addr.sin_addr.s_addr;
		sprintf(buf, "Linked to %u.%u.%u.%u port %d\n", add.b[0], add.b[1], add.b[2], add.b[3], ntohs(addr.sin_port));
		len = strlen(buf);

		if (type == AF_UNSPEC)
			cr2lf(buf, len);
		write(1, buf, len);
	}

	first = 1;

	/* Main loop */
	for (;;)
	{

		FD_ZERO(&read_fdset);
		FD_SET(fd, &read_fdset);
		FD_SET(0, &read_fdset);

		if (select(fd + 1, &read_fdset, 0, 0, 0) == -1)
		{
			printf("select %s\n", strerror(errno));
			break;
		}

		if (FD_ISSET(fd, &read_fdset))
		{
		/* BBS Socket paclen limited to ROSE_MTU bytes to match with L3 */
			len = read(fd, buf, ROSE_MTU);
			if (len <= 0)
				break;

			/* Wait for the login prompt */
			if (first)
			{
				if (strstr(buf, "allsig") || strstr(buf, "login"))
				{
					strcat(caller, "\r");
					write(fd, caller, strlen(caller));
					first = 0;
				}
			}
			else
			{
				if (type == AF_UNSPEC)
					cr2lf(buf, len);

				nb = write(STDOUT_FILENO, buf, len);
				if (nb < 0)
					break;

			}
		}

		if (FD_ISSET(STDIN_FILENO, &read_fdset))
		{
			len = read(STDIN_FILENO, buf, sizeof(buf));
			if (len <= 0)
				break;

			if (type == AF_UNSPEC)
				lf2cr(buf, len);

			nb = write(fd, buf, len);
			if (nb < 0)
				break;
		}

	}
	sleep(1);

	close(fd);

	return 0;
}

