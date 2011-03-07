/*
 * mkissattach.c
 *
 * FPAC project
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <signal.h>
#include <ctype.h>
#include <netdb.h>
#include <syslog.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <net/if.h>

#include "config.h"
#include "../pathnames.h"
#include "ax25compat.h"

static char *callsign;
static int  speed   = 0;
static int  mtu     = 0;
static int  logging = FALSE;
static void SignalTERM(int);

/************************************************************************************
* Signal handler
************************************************************************************/
static void SignalTERM(int code)
{
	syslog(LOG_INFO, "terminating on SIGTERM\n");
	closelog();
	exit(0);
}

static int readconfig(char *port)
{
	FILE *fp;
	char buffer[90], *s;
	int n = 0;
	
	if ((fp = fopen(CONF_AXPORTS_FILE, "r")) == NULL) {
		fprintf(stderr, "mkissattach: cannot open axports file\n");
		return FALSE;
	}

	while (fgets(buffer, 90, fp) != NULL) {
		n++;
	
		if ((s = strchr(buffer, '\n')) != NULL)
			*s = '\0';

		if (strlen(buffer) > 0 && *buffer == '#')
			continue;

		if ((s = strtok(buffer, " \t\r\n")) == NULL) {
			fprintf(stderr, "mkissattach: unable to parse line %d of the axports file\n", n);
			return FALSE;
		}
		
		if (strcmp(s, port) != 0)
			continue;
			
		if ((s = strtok(NULL, " \t\r\n")) == NULL) {
			fprintf(stderr, "mkissattach: unable to parse line %d of the axports file\n", n);
			return FALSE;
		}

		callsign = strdup(s);

		if ((s = strtok(NULL, " \t\r\n")) == NULL) {
			fprintf(stderr, "mkissattach: unable to parse line %d of the axports file\n", n);
			return FALSE;
		}

		speed = atoi(s);

		if ((s = strtok(NULL, " \t\r\n")) == NULL) {
			fprintf(stderr, "mkissattach: unable to parse line %d of the axports file\n", n);
			return FALSE;
		}

		if (mtu == 0) {
			if ((mtu = atoi(s)) <= 0) {
				fprintf(stderr, "mkissattach: invalid paclen setting\n");
				return FALSE;
			}
		}

		fclose(fp);
		
		return TRUE;
	}
	
	fclose(fp);

	fprintf(stderr, "mkissattach: cannot find port %s in axports\n", port);
	
	return FALSE;
}


static int setifcall(int fd, char *name)
{
	char call[7];

	if (ax25_aton_entry(name, call) == -1)
		return FALSE;
	
	if (ioctl(fd, SIOCSIFHWADDR, call) != 0) {
		close(fd);
		perror("mkissattach: SIOCSIFHWADDR");
		return FALSE;
	}

	return TRUE;
}


static int startiface(char *dev, struct hostent *hp)
{
	struct ifreq ifr;
	int fd;
	
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("mkissattach: socket");
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
			perror("mkissattach: SIOCSIFADDR");
			return FALSE;
		}
	}

	ifr.ifr_mtu = mtu;

	if (ioctl(fd, SIOCSIFMTU, &ifr) < 0) {
		perror("mkissattach: SIOCSIFMTU");
		return FALSE;
	}

	if (ioctl(fd, SIOCGIFFLAGS, &ifr) < 0) {
		perror("mkissattach: SIOCGIFFLAGS");
		return FALSE;
	}

	ifr.ifr_flags &= IFF_NOARP;
	ifr.ifr_flags |= IFF_UP;
	ifr.ifr_flags |= IFF_RUNNING;

	if (ioctl(fd, SIOCSIFFLAGS, &ifr) < 0) {
		perror("mkissattach: SIOCSIFFLAGS");
		return FALSE;
	}
	
	close(fd);
	
	return TRUE;
}
	

int main(int argc, char *argv[])
{
	int  fd;
	int  disc = N_AX25;
	char dev[64];
	int  v = 4;
	struct hostent *hp = NULL;
	struct sigaction act, oact;

	while ((fd = getopt(argc, argv, "i:lm:v")) != -1) {
		switch (fd) {
			case 'i':
				if ((hp = gethostbyname(optarg)) == NULL) {
					fprintf(stderr, "mkissattach: invalid internet name/address - %s\n", optarg);
					return 1;
				}
				break;
			case 'l':
				logging = TRUE;
				break;
			case 'm':
				if ((mtu = atoi(optarg)) <= 0) {
					fprintf(stderr, "mkissattach: invalid mtu size - %s\n", optarg);
					return 1;
				}
				break;
			case 'v':
				printf("mkissattach: %s\n", VERSION);
				return 0;
			case ':':
			case '?':
				fprintf(stderr, "usage: mkissattach [-i inetaddr] [-l] [-m mtu] [-v] ttyinterface port [ttyinterface port ...]\n");
				return 1;
		}
	}

	if ((argc - optind) < 2) {
		fprintf(stderr, "usage: mkissattach [-i inetaddr] [-l] [-m mtu] [-v] ttyinterface port [ttyinterface port ...]\n");
		return 1;
	}

	if (logging) {
		openlog("mkissattach", LOG_PID, LOG_DAEMON);	
	}
		
	while (argc - optind >= 2)  {
		if (tty_is_locked(argv[optind])) {
			fprintf(stderr, "mkissattach: device %s already in use\n", argv[optind]);
			return 1;
		}

		if (!readconfig(argv[optind + 1]))
			return 1;

		if ((fd = open(argv[optind], O_RDONLY | O_NONBLOCK)) == -1) {
			perror("mkissattach: open");
			return 1;
		}

		if (speed != 0 && !tty_speed(fd, speed))
			return 1;
	
		if (ioctl(fd, TIOCSETD, &disc) == -1) {
			perror("mkissattach: TIOCSETD");
			return 1;
		}
	
		if (ioctl(fd, SIOCGIFNAME, dev) == -1) {
			perror("mkissattach: SIOCGIFNAME");
			return 1;
		}
	
		if (!setifcall(fd, callsign))
			return 1;

		/* Now set the encapsulation */
		if (ioctl(fd, SIOCSIFENCAP, &v) == -1) {
			perror("mkissattach: SIOCSIFENCAP");
			return 1;
		}
		
		if (!startiface(dev, hp))
			return 1;		
	
		printf("AX.25 port %s bound to device %s\n", argv[optind + 1], dev);
		if (logging) syslog(LOG_INFO, "AX.25 port %s bound to device %s\n", argv[optind + 1], dev);
		if (!tty_lock(argv[optind]))
			return 1;
			
		optind += 2;	
	}
		
/* initialize signals handler */
	act.sa_handler = SignalTERM;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGTERM, &act, &oact);

	/*
	 * Become a daemon if we can.
	 */
	if (!daemon_start(FALSE)) {
		fprintf(stderr, "mkissattach: cannot become a daemon\n");
		return 1;
	}

	while (1)
		sleep(10000);
		
	/* NOT REACHED */
	return 0;
}
