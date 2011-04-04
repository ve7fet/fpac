/*
 * fpacshell.c
 * FPAC Project - F6FBB 1997
 *
 * From axspawn.c,v 1.6 1996/08/24 22:33:05 jreuter Exp jreuter
 *
 * Most parts of this program are taken from the axspawn.c of DL1BKE.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *                
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#define QUEUE_DELAY 400			/* 400 msec */

#include <stdio.h>
#include <stdlib.h>
/*#include <fcntl.h>*/
#include <string.h>
#include <ctype.h>
#include <errno.h>
/*#include <termios.h>*/
#include <unistd.h>
#include <signal.h>
#include <utmp.h>
#include <paths.h>
#include <syslog.h>
#include <time.h>
#include <sys/types.h>
/*#include <sys/stat.h>*/
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/file.h>

#include "ax25compat.h"
#include "fpac.h"
#include "global.h"
#include "md5.h"

#define MAXLEN strlen("DB0PRA-15")
#define MINLEN strlen("KA9Q")

#define AX_PACLEN	256
#define	NETROM_PACLEN	236
#define	ROSE_PACLEN	ROSE_MTU	/* 251 */

#define IS_DIGIT(x)  ( (x >= '0') && (x <= '9') )
#define IS_LETTER(x) ( (x >= 'A') && (x <= 'Z') )

#define MSG_NOCALL	"Sorry, you are not allowed to connect.\n"
#define MSG_CANNOTFORK	"Sorry, system is overloaded.\n"
#define MSG_NOPTY	"Sorry, all channels in use.\n"
#define MSG_NOCFG	"Sorry, configuration file not found.\n"

#define EXITDELAY	1

int paclen = 128;				/* Its the shortest ie safest */
int crlf = 1;

char *user_shell = "/bin/sh";

struct write_queue
{
	struct write_queue *next;
	char *data;
	int len;
};

struct write_queue *wqueue_head = NULL;
struct write_queue *wqueue_tail = NULL;
long wqueue_length = 0;

int _write_ax25 (const char *s, int len)
{
	int k, m;
	char *p;

	p = (char *) malloc (len + 1);

	if (p == NULL)
		return 0;

	m = 0;
	for (k = 0; k < len; k++)
	{
		if (crlf)
		{
			if ((s[k] == '\r') && ((k + 1) < len) && (s[k + 1] == '\n'))
				continue;
			else if (s[k] == '\n')
				p[m++] = '\r';
			else
				p[m++] = s[k];
		}
		else
			p[m++] = s[k];
	}

	if (m)
		write (1, p, m);

	free (p);
	return len;
}

int read_ax25 (char *s, int size)
{
	int len = read (0, s, size);
	int k;

	if (crlf)
		for (k = 0; k < len; k++)
			if (s[k] == '\r')
				s[k] = '\n';

	return len;
}

/*
 *  We need to buffer the data from the pipe since bash does
 *  a fflush() on every output line. We don't want it, it's
 *  PACKET radio, isn't it?
 */

void kick_wqueue (int dummy)
{
	char *s, *p;
	struct write_queue *buf;


	if (wqueue_length == 0)
		return;

	s = (char *) malloc (wqueue_length);

	p = s;

	while (wqueue_head)
	{
		buf = wqueue_head;
		wqueue_head = buf->next;

		memcpy (p, buf->data, buf->len);
		p += buf->len;
		free (buf->data);
		free (buf);
	}

	_write_ax25 (s, wqueue_length);
	free (s);
	wqueue_tail = NULL;
	wqueue_length = 0;
}

int write_ax25 (const char *s, int len)
{
	struct itimerval itv, oitv;
	struct write_queue *buf;

	signal (SIGALRM, SIG_IGN);

	buf = (struct write_queue *) malloc (sizeof (struct write_queue));

	if (buf == NULL)
		return 0;

	buf->data = (char *) malloc (len);
	if (buf->data == NULL)
		return 0;

	memcpy (buf->data, s, len);
	buf->len = len;
	buf->next = NULL;

	if (wqueue_head == NULL)
	{
		wqueue_head = buf;
		wqueue_tail = buf;
		wqueue_length = len;
	}
	else
	{
		wqueue_tail->next = buf;
		wqueue_tail = buf;
		wqueue_length += len;
	}

	if (wqueue_length >= paclen)
	{
		kick_wqueue (0);
	}
	else
	{
		itv.it_interval.tv_sec = 0;
		itv.it_interval.tv_usec = 0;
		itv.it_value.tv_sec = 0;
		itv.it_value.tv_usec = QUEUE_DELAY;

		setitimer (ITIMER_REAL, &itv, &oitv);
		signal (SIGALRM, kick_wqueue);
	}
	return len;
}

void cleanup (char *tty)
{
}


int child_pid;

void signal_handler (int dummy)
{
	kill (child_pid, SIGHUP);
	exit (1);
}

int md_check (cfg_t * cfg)
{
	time_t now;
	int len = 0;
	int i;
	int cnt;
	int net_pos[5];
	char netrom[6];
	char temp[256];
	char buf[256];
	char hostname[128];
/*	char md2str[33]; */
	char md5str[33];

	time (&now);

	len = strlen (cfg->password);

	srandom(time(NULL));
	
	for (i = 0; i < 5; i++)
	{
		net_pos[i] = (random () % len);
		netrom[i] = cfg->password[net_pos[i]];
	}
	netrom[i] = '\0';

	gethostname (hostname, sizeof (hostname));

	/*limit hostname to 20 characters */
	hostname[20] = '\0';

	sprintf (temp, "%s> %d %d %d %d %d [%010ld]\n",
			 hostname,
			 net_pos[0] + 1,
			 net_pos[1] + 1,
			 net_pos[2] + 1,
			 net_pos[3] + 1,
			 net_pos[4] + 1,
			 now);

	write_ax25 (temp, strlen (temp));

	sprintf (temp, "%010d%s", (int) now, cfg->password);
/*	MD2String (md2str, temp); */
	MD5String (md5str, temp);

	cnt = read_ax25 (buf, sizeof (buf));
	if (cnt < 0)				/* Connection died */
	{
		return 0;
	}

	buf[cnt] = '\0';
	for (i = 0; buf[i]; ++i)
	{
		if (buf[i] < ' ')
		{
			buf[i] = '\0';
			break;
		}
	}

	if (*buf)
	{
		/* Plain text */
		if (!strcasecmp (buf, cfg->password))
			return 1;
		/* NetRom */
		if (!strcasecmp (buf, netrom))
			return 1;
		/* MD5 */
		if (!strcasecmp (buf, md5str))
			return 1;
		/* MD2 */
		/* if (!strcasecmp (buf, md2str))
			return 1; */
	}
	return 0;
}

int pty_open (int *pid, struct winsize *winsize, char **chargv, char **chenvp)
{
	int pty = -1;
	int i;
	char line[20];
	int c;
	int tty, devtty;

	for (c = 'a'; c <= 'z'; c++)
	{
		for (i = 0; i < 16; i++)
		{
			sprintf (line, "/dev/pty%c%x", c, i);
			pty = open (line, O_RDWR | O_NOCTTY);
			if (pty >= 0)
				break;
		}
		if (pty >= 0)
			break;
	}

	if (pty < 0)
	{
		fprintf (stderr, "Out of pty\n");
		return -1;
	}

	ioctl (pty, TIOCEXCL, NULL);

	if ((*pid = fork ()) != 0)
	{
		/* Father */
		return pty;
	}

	/* Child */

	close (pty);

	setenv ("TERM", "linux", 1);

	line[5] = 't';
	tty = open (line, O_RDWR);
	if (tty < 0)
	{
		fprintf (stderr, "Cannot open slave side\n");
		close (pty);
		return -1;
	}
	(void) chown (line, getuid (), getgid ());
	(void) chmod (line, 0600);

	setsid ();					/* will break terminal affiliation */
	ioctl (tty, TIOCSCTTY, (char *) 0);

	setuid (getuid ());
	setgid (getgid ());

	devtty = open ("/dev/tty", O_RDWR);
	if (devtty < 0)
	{
		perror ("cannot open /dev/tty");
		exit (1);
	}

/*      if (ioctl(devtty, TIOCNOTTY, (char *)0)) {
   perror("cannot do iotctl TIOCNOTTY");
   exit(1);
   }
 */
	ioctl (devtty, TIOCSWINSZ, winsize);
	close (tty);
	dup2 (devtty, 0);
	dup2 (devtty, 1);
	dup2 (devtty, 2);
	execve (chargv[0], chargv, chenvp);
	exit (0);
}

int main (int argc, char **argv)
{
	char call[20];
	char buf[2048];
	int i, k, cnt, digits, letters, invalid, ssid, ssidcnt;
	unsigned int addrlen;
	int fdmaster;
	int identify = TRUE;
	pid_t pid = -1;
	fd_set fds_read;
	int chargc;
	char *chargv[20];
	int envc;
	char *envp[20];
	struct winsize win = {24, 80, 0, 0};
	union
	{
		struct full_sockaddr_ax25 fsax25;
		struct full_sockaddr_rose rose;
	}
	sockaddr;
	char *protocol;
	cfg_t cfg;

	if ((argc > 1) && (strcmp(argv[1], "-n") == 0))
	{
		identify = FALSE;
		--argc;
		++argv;
	}
		
	digits = letters = invalid = ssid = ssidcnt = 0;

	openlog ("fpacshell", LOG_PID, LOG_DAEMON);

	addrlen = sizeof (struct full_sockaddr_ax25);

	memset(&sockaddr.rose, 0x00, sizeof(struct full_sockaddr_rose));

	k = getpeername (0, (struct sockaddr *) &sockaddr, &addrlen);

	if (k == 0)
	{
	switch (sockaddr.fsax25.fsa_ax25.sax25_family)
	{
	case AF_AX25:
		strcpy (call, ax25_ntoa (&sockaddr.fsax25.fsa_ax25.sax25_call));
		protocol = "AX.25";
		paclen = AX_PACLEN;
		break;

	case AF_NETROM:
		strcpy (call, ax25_ntoa (&sockaddr.fsax25.fsa_ax25.sax25_call));
		protocol = "NET/ROM";
		paclen = NETROM_PACLEN;
		break;

	case AF_ROSE:
		strcpy (call, ax25_ntoa (&sockaddr.rose.srose_call));
		protocol = "Rose";
		paclen = ROSE_PACLEN;
		break;

	default:
/*		syslog (LOG_NOTICE, "peer is not an AX.25, NET/ROM or Rose socket\n"); */
		strcpy (call, "UNKNOWN");
		protocol = "SHELL";
		crlf = 0;
		paclen = AX_PACLEN;
		break;
	}
	}
	else
	{
		strcpy (call, "UNKNOWN");
		protocol = "SHELL";
		crlf = 0;
		paclen = AX_PACLEN;
	}

	/* Get FPAC configuration */
	if (cfg_open (&cfg) != 0)
	{
		write_ax25 (MSG_NOCFG, sizeof (MSG_NOCFG));
		syslog (LOG_NOTICE, "FPAC configuration file not found\n");
		sleep (EXITDELAY);
		return 1;
	}

	/* Check for MD password */
	if ((identify) && (!md_check (&cfg)))
		return 2;

	if (argc == 1)
	{
		chargc = 0;
		chargv[chargc++] = user_shell;
	}
	else
	{
/*		int i;*/
		for (i = 0 ; i < (argc-1) ; i++)
		{
			if (i == 20)
				break;
			chargv[i] = argv[i+1];
		}
		chargc = argc-1;
	}
	chargv[chargc] = NULL;

	envc = 0;
	envp[envc] = (char *) malloc (30);
	sprintf (envp[envc++], "AXCALL=%s", call);
	envp[envc] = (char *) malloc (30);
	sprintf (envp[envc++], "PROTOCOL=%s", protocol);
		/* envp[envc] = (char *) malloc (30);
		sprintf (envp[envc++], "TERM=linux"); */
	envp[envc] = NULL;

	fdmaster = pty_open(&pid, &win, chargv, envp);

	if (fdmaster == -1)
		return 4;
		
	if (pid != -1)
	{
		child_pid = 0;
		signal (SIGHUP, signal_handler);
		signal (SIGTERM, signal_handler);
		signal (SIGINT, signal_handler);
		signal (SIGQUIT, signal_handler);

		while (1)
		{
			FD_ZERO (&fds_read);
			FD_SET (0, &fds_read);
			FD_SET (fdmaster, &fds_read);

			k = select (fdmaster + 1, &fds_read, NULL, NULL, NULL);

			if (k > 0)
			{
				if (FD_ISSET (0, &fds_read))
				{
					cnt = read_ax25 (buf, sizeof (buf));
					if (cnt < 0)	/* Connection died */
					{
						break;
					}
					else
						write (fdmaster, buf, cnt);
				}

				if (FD_ISSET (fdmaster, &fds_read))
				{
					cnt = read (fdmaster, buf, sizeof (buf));
					if (cnt < 0)
					{
						break;
					}
					write_ax25 (buf, cnt);
				}
			}
			else if (k < 0 && errno != EINTR)
			{
				break;
			}
		}
	}
	else
	{
		syslog (LOG_ERR, "cannot fork %m, closing connection to %s\n", call);
		write_ax25 (MSG_CANNOTFORK, sizeof (MSG_CANNOTFORK));
		sleep (EXITDELAY);
		return 1;
	}

	sleep (EXITDELAY);

	return 0;
}
