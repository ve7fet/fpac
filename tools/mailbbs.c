/*
* mailbbs.c
* This creates the mailbbs binary.
*
* mailbbs sends a file, or its standard input, to a BBS using
* the standard BBS forwarding protocol
*
* Most of code is from "call" of ax25apps
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
#include <errno.h>
#include <signal.h>
#include <sys/select.h>

#include "config.h"
#include "ax25compat.h"
#include "fpac.h"

#define WAITSID 1
#define WAIT1STPROMPT 2
#define WAIT2NDPROMPT 3
#define WAITOK 4
#define WAITLASTPROMPT 5
#define PROC_NR_NODES_FILE "/proc/net/nr_nodes"

#ifndef LINE_TIMEOUT
#define CONNECT_TIMEOUT	120	/* seconds to get the connection */
#define LINE_TIMEOUT	60	/* seconds without a line from the BBS */
#define TOTAL_TIMEOUT	300	/* seconds for the whole dialogue */
#endif

#define RC_USAGE	1	/* bad arguments or configuration */
#define RC_CONNECT	2	/* the BBS could not be connected */
#define RC_DIALOGUE	3	/* no answer, unexpected answer or refusal */

#define READ_CLOSED	-1
#define READ_TIMEOUT	-2

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

	if ((fp = fopen (PROC_NR_NODES_FILE, "r")) == NULL)
	{
		fprintf (stderr, "call: NET/ROM not included in the kernel\n");
		return -1;
	}
	if (fgets (buffer, 100, fp) == NULL)
	{
		fclose(fp);
		return -1;
	}

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

/*
 * Reads one line of the BBS, up to and including its CR, into buffer (at
 * most maxlen bytes, plus a final \0). Returns its length, READ_CLOSED when
 * the BBS has gone or READ_TIMEOUT when nothing came for timeout seconds.
 * The timeout used to be ignored: mailbbs stayed connected for ever.
 */
static int readbbs (int fd, char *buffer, int maxlen, int timeout, int verbose)
{
	static int len = 0;
	static char localbuf[1024];
	fd_set sock_read;
	struct timeval to;
	char *cr;
	int nb;

	for (;;)
	{
		/* A LF after the CR of the previous line is not part of a line */
		while (len > 0 && localbuf[0] == '\n')
			memmove (localbuf, localbuf + 1, --len);

		cr = (len > 0) ? memchr (localbuf, '\r', (size_t) len) : NULL;
		nb = cr ? (int) (cr - localbuf) + 1 : 0;
		/* A line longer than the buffers is handed over in pieces */
		if (nb == 0 && (len >= maxlen || len == (int) sizeof (localbuf)))
			nb = len;
		if (nb > maxlen)
			nb = maxlen;

		if (nb > 0)
		{
			memcpy (buffer, localbuf, nb);
			buffer[nb] = '\0';
			len -= nb;
			memmove (localbuf, localbuf + nb, len);
			if (verbose)
				printf ("%s\n", buffer);
			return nb;
		}

		to.tv_usec = 0;
		to.tv_sec = timeout;

		FD_ZERO (&sock_read);
		FD_SET (fd, &sock_read);

		nb = select (fd + 1, &sock_read, NULL, NULL, &to);
		if (nb == -1)
		{
			if (errno == EINTR)
				continue;
			perror ("mailbbs: select");
			return READ_CLOSED;
		}
		if (nb == 0)
			return READ_TIMEOUT;

		nb = read (fd, localbuf + len, sizeof (localbuf) - len);
		if (nb <= 0)
			return READ_CLOSED;
		len += nb;
	}
}

/* Sends len bytes to the BBS, LF turned into CR. Returns -1 on error. */
static int writebbs(int fd, char *buffer, int len, int verbose)
{
	int i, nb, done = 0;
		
	if (verbose)
		fwrite (buffer, 1, len, stdout);

	for (i = 0 ; i < len ; i++)
	{
		if (buffer[i] == '\n')
			buffer[i] = '\r';
	}

	while (done < len)
	{
		nb = write (fd, buffer + done, len - done);
		if (nb < 0)
		{
			if (errno == EINTR)
				continue;
			return -1;
		}
		done += nb;
	}
	return len;
}

/*
 * Sends the text of the message: the file, or the standard input when
 * there is no file (filefd 0). Returns the number of bytes sent, -1 on
 * error. *last receives the last character sent.
 */
static int sendfile(int fd, int filefd, int paclen, int verbose, char *last)
{
	int nb;
	int total = 0;
	char *buffer;
	
	buffer = malloc(paclen);
	if (buffer == NULL)
		return -1;
		
	for (;;)
	{
		nb = read(filefd, buffer, paclen);
		if (nb < 0 && errno == EINTR)
			continue;
		if (nb <= 0)
			break;
		*last = buffer[nb - 1];
		if (writebbs(fd, buffer, nb, verbose) < 0)
		{
			free(buffer);
			return -1;
		}
		total += nb;
	}
	
	free(buffer);
		
	return total;
}

static void usage (FILE *f)
{
	fprintf (f, "usage: mailbbs -d callsign[@bbs] [-f filename] [-i sender] [-t title] [-v] port bbs-callsign [[via] digis...]\n"
		"Leaves a personal message on a BBS, as a user would.\n"
		"-d : recipient of the message (needed)\n"
		"-f : text of the message; without -f the standard input is sent,\n"
		"     unless it is a terminal (the message then has no text)\n"
		"-i : callsign the message is sent from (default: the one of the port)\n"
		"-t : title (default: \"message fm mailbbs\")\n"
		"-v : show the dialogue with the BBS\n"
		"-V : show the version and exit      -h : this help\n"
		"port is an AX.25, NET/ROM or ROSE port; for ROSE: port bbs-callsign rose-address [[via] digi]\n"
		"exit code: 0 message left, 1 bad arguments, 2 no connection, 3 no or bad answer from the BBS\n");
}

static void on_alarm (int sig)
{
	/* Only there to interrupt connect() */
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
	int filefd = -1;	/* -1: no text, 0: standard input, else the file */
	char last = '\n';
	time_t deadline;
	int wait;
	struct sigaction act;

	while ((p = getopt (argc, argv, "d:f:i:t:vVh")) != -1)
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
		case 'V':
			printf ("mailbbs %s\n", VERSION);
			return 0;
		case 'h':
			usage (stdout);
			return 0;
		case '?':
		case ':':
			if (optind > 1 && strcmp (argv[optind - 1], "-?") == 0)
			{
				usage (stdout);
				return 0;
			}
			usage (stderr);
			return RC_USAGE;
		}
	}

	if (optind == argc || optind == argc - 1)
	{
		usage (stderr);
		return RC_USAGE;
	}

	port = argv[optind];

	if (mailto == NULL)
	{
		fprintf (stderr, "message recipient missing (option -d)\n");
		return 1;
	}

	if (title == NULL)
	{
		title = "message fm mailbbs";
	}

	/* The text is opened before the BBS is called: a missing file must
	 * not leave a message without text, nor a connection for nothing */
	if (filename)
	{
		filefd = open (filename, O_RDONLY);
		if (filefd < 0)
		{
			fprintf (stderr, "mailbbs: cannot open %s: %s\n", filename, strerror (errno));
			return RC_USAGE;
		}
	}
	else if (!isatty (STDIN_FILENO))
	{
		/* The header of this file always said that the standard input
		 * was sent; it never was without -f */
		filefd = STDIN_FILENO;
	}

	if (ax25_config_load_ports () == 0)
	{
		fprintf (stderr, "mailbbs: no AX.25 port data configured\n");
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
				fprintf (stderr, "mailbbs: invalid port setting\n");
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
			fprintf (stderr, "mailbbs: too few arguments for Rose\n");
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
		if (call == NULL)
		{
			fprintf (stderr, "mailbbs: cannot determine local AX.25 callsign (use -i)\n");
			close (fd);
			return (-1);
		}
		ax25_aton_entry(call, sockaddr.rose.srose_call.ax25_call);
		{
			char *rs_addr = rs_get_addr(NULL);
			if (rs_addr == NULL)
			{
				fprintf (stderr, "mailbbs: cannot determine local ROSE address (ROSE not configured?)\n");
				close (fd);
				return (-1);
			}
			rose_aton(rs_addr, sockaddr.rose.srose_addr.rose_addr);
		}
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
		if (call == NULL)
		{
			fprintf (stderr, "mailbbs: cannot determine local NET/ROM callsign (use -i)\n");
			close (fd);
			return (-1);
		}
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
		if (call == NULL)
		{
			fprintf (stderr, "mailbbs: cannot determine local AX.25 callsign (use -i)\n");
			close (fd);
			return (-1);
		}
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

	/* Connect the BBS, without waiting for ever */
	memset (&act, 0, sizeof (act));
	act.sa_handler = on_alarm;
	sigemptyset (&act.sa_mask);
	sigaction (SIGALRM, &act, NULL);	/* no SA_RESTART: connect() returns EINTR */
	signal (SIGPIPE, SIG_IGN);

	alarm (CONNECT_TIMEOUT);
	if (connect (fd, (struct sockaddr *) &sockaddr, addrlen))
	{
		if (errno == EINTR)
			fprintf (stderr, "mailbbs: no connection to %s after %d seconds\n", av[0], CONNECT_TIMEOUT);
		else
			fprintf (stderr, "mailbbs: cannot connect %s: %s\n", av[0], strerror (errno));
		close (fd);
		return RC_CONNECT;
	}
	alarm (0);

	if (verbose)
		printf ("*** Connected to %s\n", av[0]);

	phase = WAITSID;
	deadline = time (NULL) + TOTAL_TIMEOUT;

	for (;;)
	{
		static const char *waiting[] = { "", "the banner [...] of the BBS", "the first prompt",
			"the prompt after our banner", "the answer to SP", "the prompt after the message" };

		wait = (int) (deadline - time (NULL));
		if (wait > LINE_TIMEOUT)
			wait = LINE_TIMEOUT;
		len = (wait > 0) ? readbbs (fd, buf, sizeof (buf) - 1, wait, verbose) : READ_TIMEOUT;
		if (len <= 0)
		{
			/* Disconnection or timeout */
			fprintf (stderr, "mailbbs: %s while waiting for %s%s\n",
				(len == READ_TIMEOUT) ? "no answer from the BBS" : "disconnected by the BBS",
				waiting[phase],
				(phase == WAITLASTPROMPT) ? " (the message may have been taken)" : ": message not sent");
			close (fd);
			return RC_DIALOGUE;
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
			if (len >= 2 && buf[len - 2] == '>')
			{
				/* 1st prompt received */
				/* Send SID */
				sprintf(buf, "[MAIL-%s-$]\n", VERSION);
				if (writebbs(fd, buf, strlen(buf), verbose) < 0)
					goto lost;
				phase = WAIT2NDPROMPT;
			}
			break;
		case WAIT2NDPROMPT:
			if (len >= 2 && buf[len - 2] == '>')
			{
				/* 2nd prompt received */
				/* Send proposal */
				snprintf(buf, sizeof(buf), "SP %s\n", mailto);
				if (writebbs(fd, buf, strlen(buf), verbose) < 0)
					goto lost;
				phase = WAITOK;
			}
			break;
		case WAITOK:
			if (buf[0] == 'O' || buf[0] == 'o')
			{
				/* Send title */
				snprintf(buf, sizeof(buf), "%s\n", title);
				if (writebbs(fd, buf, strlen(buf), verbose) < 0)
					goto lost;

				/* Send text */
				if (filefd >= 0 && sendfile(fd, filefd, paclen, verbose, &last) < 0)
					goto lost;
				
				/* End the mail: Ctrl-Z alone on its line */
				sprintf(buf, "%s\032\n", (last == '\n' || last == '\r') ? "" : "\n");
				if (writebbs(fd, buf, strlen(buf), verbose) < 0)
					goto lost;

				phase = WAITLASTPROMPT;
				break;
			}
			else
			{
				/* Refused */
				buf[strcspn (buf, "\r\n")] = '\0';
				fprintf (stderr, "mailbbs: the BBS refused the message for %s: %s\n", mailto, buf);
				close (fd);
				return RC_DIALOGUE;
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

lost:
	fprintf (stderr, "mailbbs: connection lost while sending: %s\n", strerror (errno));
	close (fd);
	return RC_DIALOGUE;
}
