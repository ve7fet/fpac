/*
 * mailbbs.c
 *
 * FPAC project
 *
 * mailbbs : sends the stdin to a BBS using 
 * the standard BBS forwarding protocol
 *
 * F6FBB 10-98
 *
 * Most of code from "call" of ax25utils
 *
 */
 
/*#include <sys/types.h>*/
/*#include <utime.h>*/
#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
/*#include <sys/stat.h>*/
#include <time.h>
/*#include <sys/wait.h>*/
#include <unistd.h>

#include "config.h"
#include "ax25compat.h"
#include "fpac.h"

#define WAITSID 1
#define WAIT1STPROMPT 2
#define WAIT2NDPROMPT 3
#define WAITOK 4
#define WAITLASTPROMPT 5

union
{
	struct full_sockaddr_ax25 ax25;
	struct sockaddr_rose rose;
}
sockaddr;

static int nr_ax25_aton (char *address, struct full_sockaddr_ax25 *addr)
{
	char buffer[100], *call, *alias;
	FILE *fp;
	int addrlen;

	for (call = address; *call != '\0'; call++)
		*call = toupper (*call);

#define PROC_NR_NODES_FILE "/proc/net/nr_nodes"

	if ((fp = fopen (PROC_NR_NODES_FILE, "r")) == NULL)
	{
		fprintf (stderr, "call: NET/ROM not included in the kernel\n");
		return -1;
	}
	fgets (buffer, 100, fp);

	while (fgets (buffer, 100, fp) != NULL)
	{
		call = strtok (buffer, " \t\n\r");
		alias = strtok (NULL, " \t\n\r");

		if (strcmp (address, call) == 0 || strcmp (address, alias) == 0)
		{
			addrlen = ax25_aton (call, addr);
			fclose (fp);
			return (addrlen == -1) ? -1 : sizeof (struct sockaddr_ax25);
		}
	}

	fclose (fp);

	fprintf (stderr, "call: NET/ROM callsign or alias not found\n");

	return -1;
}

static int readbbs (int fd, char *buffer, int maxlen, int timeout, int verbose)
{
	fd_set sock_read;
	struct timeval to;
	int nb;
	int line;
	int i;
	int j;
	int pos = 0;
	static int len = 0;
	static char localbuf[1024];

	for (;;)
	{
		line = 0;
		j = 0;
		nb = 0;
		for (i = 0; i < len; i++)
		{
			if (i == maxlen)
			{
				nb = i+1;
				line = 1;
			}
			else if (line == 0)
			{
				buffer[pos++] = localbuf[i];
				if (localbuf[i] == '\r')
				{
					nb = i+1;
					line = 1;
				}
			}
			else
			{
				/* Compact localbuf */
				localbuf[j++] = localbuf[i];
			}

		}
		len = j;

		if (line)
			break;

		to.tv_usec = 0;
		to.tv_sec = timeout;

		FD_ZERO (&sock_read);
		FD_SET (fd, &sock_read);

		if (select (fd + 1, &sock_read, NULL, NULL, &to) == -1)
		{
			perror ("select");
			return -1;
		}

		if (FD_ISSET (fd, &sock_read))
		{
			nb = read (fd, localbuf + len, sizeof (localbuf) - len);
			if (nb <= 0)
			{
				/* Disconnection */
				return -1;
			}
			len += nb;
		}
	}
	
	if (verbose)
	{
	buffer[nb] = '\0';
	printf ("%s\n", buffer);
	}

	return nb;
}

static int writebbs(int fd, char *buffer, int len, int verbose)
{
	int i;
	
	if (verbose)
	{
	buffer[len] = '\0';
	printf("%s", buffer);
	}
		
	for (i = 0 ; i < len ; i++)
	{
		if (buffer[i] == '\n')
			buffer[i] = '\r';
	}
	
	return write(fd, buffer, len);
}

static int sendfile(int fd, char *filename, int paclen, int verbose)
{
	int nb;
	int total = 0;
	int filefd = 0;
	char *buffer;
	
	if (filename)
	{
		filefd = open(filename, O_RDONLY);
		if (filefd < 0)
			return -1;
	}
	
	buffer = malloc(paclen);
	if (buffer == NULL)
		return -1;
		
	for (;;)
	{
		nb = read(filefd, buffer, paclen);
		if (nb <= 0)
			break;
		writebbs(fd, buffer, nb, verbose);
		total += nb;
	}
	
	free(buffer);
	if (filefd)
		close(filefd);
		
	return total;
}

int main (int argc, char **argv)
{
	int fd = 0;
	int p;
	int paclen = 256;
	int af_mode;
	int addrlen = 0;
	int len;
	int phase;
	int verbose = 0;

	char **av;
	char *digi;
	char *mailto = NULL;
	char *title = NULL;
	char *port = NULL;
	char *filename = NULL;
	char *call = NULL;
	char buf[256];

	while ((p = getopt (argc, argv, "d:f:i:p:t:v")) != -1)
	{
		switch (p)
		{
		case 'd':
			mailto = optarg;
			break;
		case 'f':
			filename = optarg;
			break;
		case 'i':
			call = optarg;
			break;
		case 't':
			title = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		case '?':
		case ':':
			fprintf (stderr, "usage: mail_bbs [-d callsign@bbs] [-f filename] [-i sender] [-t title] [-v] port bbs-callsign [[via] digis...]\n");
			return 1;
		}
	}

	if (optind == argc || optind == argc - 1)
	{
		fprintf (stderr, "usage: mail_bbs [-d callsign@bbs] [-f filename] [-i sender] [-t title] [-v] port bbs-callsign [[via] digis...]\n");
		return 1;
	}

	port = argv[optind];

	if (mailto == NULL)
	{
		fprintf (stderr, "message recipient missing (option -d)\n");
		return 1;
	}

	if (title == NULL)
	{
		title = "message fm mail_bbs";
	}

	if (ax25_config_load_ports () == 0)
	{
		fprintf (stderr, "mail_bbs: no AX.25 port data configured\n");
		return 1;
	}

	if (ax25_config_get_addr (port) == NULL)
	{
		nr_config_load_ports ();

		if (nr_config_get_addr (port) == NULL)
		{
			rs_config_load_ports ();

			if (rs_config_get_addr (port) == NULL)
			{
				fprintf (stderr, "mail_bbs: invalid port setting\n");
				return 1;
			}
			else
			{
				af_mode = AF_ROSE;
			}
		}
		else
		{
			af_mode = AF_NETROM;
		}
	}
	else
	{
		af_mode = AF_AX25;
	}

	av = argv + optind + 1;

	switch (af_mode)
	{
	case AF_ROSE:
		paclen = rs_config_get_paclen (port);
		if (av[0] == NULL || av[1] == NULL)
		{
			fprintf (stderr, "mail_bbs: too few arguments for Rose\n");
			return (-1);
		}
		if ((fd = socket (AF_ROSE, SOCK_SEQPACKET, 0)) < 0)
		{
			perror ("socket");
			return (-1);
		}

		sockaddr.rose.srose_family = AF_ROSE;
		sockaddr.rose.srose_ndigis = 0;
		if (call == NULL)
			call = ax25_config_get_addr (NULL);
		ax25_aton_entry(call, sockaddr.rose.srose_call.ax25_call);
		rose_aton(rs_get_addr(NULL), sockaddr.rose.srose_addr.rose_addr);
		addrlen = sizeof(struct full_sockaddr_rose);
		if (bind (fd, (struct sockaddr *) &sockaddr, addrlen) == -1)
		{
			perror ("bind");
			close (fd);
			return (-1);
		}

		memset (&sockaddr.rose, 0x00, sizeof (struct sockaddr_rose));

		if (ax25_aton_entry (av[0], sockaddr.rose.srose_call.ax25_call) == -1)
		{
			close (fd);
			return (-1);
		}
		if (rose_aton (av[1], sockaddr.rose.srose_addr.rose_addr) == -1)
		{
			close (fd);
			return (-1);
		}
		if (av[2] != NULL)
		{
			digi = av[2];
			if (strcasecmp (av[2], "VIA") == 0)
			{
				if (av[3] == NULL)
				{
					fprintf (stderr, "call: callsign must follow 'via'\n");
					close (fd);
					return (-1);
				}
				digi = av[3];
			}
			if (ax25_aton_entry (digi, sockaddr.rose.srose_digi.ax25_call) == -1)
			{
				close (fd);
				return (-1);
			}
			sockaddr.rose.srose_ndigis = 1;
		}
		sockaddr.rose.srose_family = AF_ROSE;
		addrlen = sizeof (struct sockaddr_rose);

		break;

	case AF_NETROM:
		paclen = nr_config_get_paclen (port);
		if (av[0] == NULL)
		{
			fprintf (stderr, "call: too few arguments for NET/ROM\n");
			return (-1);
		}
		if ((fd = socket (AF_NETROM, SOCK_SEQPACKET, 0)) < 0)
		{
			perror ("socket");
			return (-1);
		}
		
		if (call == NULL)
			call = nr_config_get_addr (port);
		ax25_aton (call, &sockaddr.ax25);
		sockaddr.ax25.fsa_ax25.sax25_family = AF_NETROM;
		addrlen = sizeof (struct full_sockaddr_ax25);

		if (bind (fd, (struct sockaddr *) &sockaddr, addrlen) == -1)
		{
			perror ("bind");
			close (fd);
			return (-1);
		}

		if (nr_ax25_aton (av[0], &sockaddr.ax25) == -1)
		{
			close (fd);
			return (-1);
		}
		sockaddr.rose.srose_family = AF_NETROM;
		addrlen = sizeof (struct sockaddr_ax25);

		break;

	case AF_AX25:
		paclen = ax25_config_get_paclen (port);
		if (av[0] == NULL)
		{
			fprintf (stderr, "call: too few arguments for AX.25\n");
			return (-1);
		}
		if ((fd = socket (AF_AX25, SOCK_SEQPACKET, 0)) < 0)
		{
			perror ("socket");
			return (-1);
		}
		if (call == NULL)
			call = ax25_config_get_addr (port);
		ax25_aton (call, &sockaddr.ax25);
		sockaddr.ax25.fsa_ax25.sax25_family = AF_AX25;
		addrlen = sizeof (struct full_sockaddr_ax25);

		if (bind (fd, (struct sockaddr *) &sockaddr, addrlen) == -1)
		{
			perror ("bind");
			close (fd);
			return (-1);
		}

		if (ax25_aton_arglist ((const char **)av, &sockaddr.ax25) == -1)
		{
			close (fd);
			return (-1);
		}
		sockaddr.rose.srose_family = AF_AX25;
		addrlen = sizeof (struct full_sockaddr_ax25);

		break;
	}

	if (verbose)
		printf ("F6FBB mailbbs v%s\n", VERSION);

	/* Connect the BBS */
	if (connect (fd, (struct sockaddr *) &sockaddr, addrlen))
	{
		printf ("\n");
		perror ("connect");
		close (fd);
		return (-1);
	}

	if (verbose)
		printf ("*** Connected to %s\n", av[0]);

	phase = WAITSID;

	for (;;)
	{
		len = readbbs (fd, buf, sizeof (buf), 300, verbose);
		if (len <= 0)
		{
			/* Disconnection or timeout */
			close (fd);
			return 1;
		}

		switch (phase)
		{
		case WAITSID:
			if (len > 3 && buf[0] == '[' && buf[len - 2] == ']')
			{
				/* SID received */
				phase = WAIT1STPROMPT;
			}
			break;
		case WAIT1STPROMPT:
			if (len > 2 && buf[len - 2] == '>')
			{
				/* 1st prompt received */
				/* Send SID */
				sprintf(buf, "[MAIL-%s-$]\n", VERSION);
				writebbs(fd, buf, strlen(buf), verbose);
				phase = WAIT2NDPROMPT;
			}
			break;
		case WAIT2NDPROMPT:
			if (len >= 2 && buf[len - 2] == '>')
			{
				/* 2nd prompt received */
				/* Send proposal */
				sprintf(buf, "SP %s\n", mailto);
				writebbs(fd, buf, strlen(buf), verbose);
				phase = WAITOK;
			}
			break;
		case WAITOK:
			if (buf[0] == 'O')
			{
				/* Send title */
				sprintf(buf, "%s\n", title);
				writebbs(fd, buf, strlen(buf), verbose);

				/* Send text */
				if (filename)
				if (sendfile(fd, filename, paclen, verbose) < 0)
				{
					close(fd);
					return 1;
				}
				
				/* End the mail */
				sprintf(buf, "\032\n");
				writebbs(fd, buf, strlen(buf), verbose);

				phase = WAITLASTPROMPT;
				break;
			}
			else
			{
				/* Disconnect */
				close (fd);
				return 1;
			}
		case WAITLASTPROMPT:
			if (len >= 2 && buf[len - 2] == '>')
			{
				/* Last prompt received. Clean disconnection */
				if (verbose)
					printf ("*** Done\n");

				close (fd);
				return 0;
			}
			break;
		}
	}

	if (verbose)
		printf ("*** Disconnected fm %s\n", av[0]);

	return 0;
}
