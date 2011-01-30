/*
 * flexd.c
 * original by Jean-Paul f6fbb
 * FPAC project
 * part of code borrowed from call.c (ax25-apps) and rose_call.c (ax25-tools)
 */
#include "../config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <syslog.h>

#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <netax25/axlib.h>
#include <netax25/axconfig.h>
#include <netax25/daemon.h>
#include <netax25/nrconfig.h>
#include <netax25/rsconfig.h>

#include "../pathnames.h"
#include "procinfo.h"

#define DEFAULT_POLL_TIME 600
#define MINIMUM_POLL_TIME 300

int poll_time = DEFAULT_POLL_TIME;
static char flexgate[10] = "\0";
static char mycall[10] = "\0";
struct ax_routes *gw;
int s;
static int backoff = -1;
static int ax25mode = -1;
int verbose = 1;
int passive = 1;
int debug = 1;
int is_daemon = 1;

static void Usage(void)
{
	fprintf(stderr, "Usage : flexd [-h] [-d] [-x] [-v] [-f wpfile]\n");
	fprintf(stderr, "-h : display this message\n");
	fprintf(stderr, "-d : start in foreground mode\n");
	fprintf(stderr, "-x : turn on debug mode\n");
	fprintf(stderr, "-v : turn on debug verbose mode\n");
	fprintf(stderr, "-p : turn on passive mode\n");
	exit(1);
}

int read_conf(void)
{
	FILE *fp, *fgt;
	char buf[512], line[1024], *cp;
	int i = 0, k;
	char digipath[AX25_MAX_DIGIS * 10];
	if ((fp = fopen(FLEXD_CONF_FILE, "r")) == NULL) {
		fprintf(stderr, "flexd config: Cannot open config file: %s\n",
				FLEXD_CONF_FILE);
		return -1;
	}
	if ((fgt = fopen(FLEX_GT_FILE, "w")) == NULL) {
		fprintf(stderr,
				"flexd config: Cannot open flexnet gateways file: %s\n",
				FLEX_GT_FILE);
		fclose(fp);
		return -1;
	}

	fputs("addr  callsign  dev  digipeaters\n", fgt);

	while (fgets(buf, sizeof(buf), fp)) {
		if (*buf == '#' || *buf == ' ')
			continue;			/* comment line/empty line */
		cp = strchr(buf, '#');
		if (cp)
			*cp = '\0';
		cp = strtok(buf, " \t\n\r");
		if (cp == NULL)
			continue;			/* empty line */

		if (strcasecmp(cp, "pollinterval") == 0) {	/* set poll interval */
			cp = strtok(NULL, " \t\n\r");
			if (cp == NULL) {
				fprintf(stderr,
						"flexd config: PollInterval needs an argument\n");
				fclose(fp);
				fclose(fgt);
				return -1;
			}
			poll_time = safe_atoi(cp);
			if (poll_time < MINIMUM_POLL_TIME)
				poll_time = MINIMUM_POLL_TIME;
		}
		if (strcasecmp(cp, "mycall") == 0) {	/* set connect call for download */
			cp = strtok(NULL, " \t\n\r");
			if (cp == NULL) {
				fprintf(stderr,
						"flexd config: MyCall needs an argument\n");
				fclose(fp);
				fclose(fgt);
				return -1;
			}
			safe_strncpy(mycall, cp, 9);
		}
		if (strcasecmp(cp, "flexgate") == 0) {	/* set flexnet gateway */
			cp = strtok(NULL, " \t\n\r");
			if (cp == NULL) {
				fprintf(stderr,
						"flexd config: FlexGate needs an argument\n");
				fclose(fp);
				fclose(fgt);
				return -1;
			}
			safe_strncpy(flexgate, cp, 9);
/*	strcat(cp,"\0");*/
			gw = find_route(flexgate, NULL);
			if (gw == NULL) {
				fprintf(stderr,
						"flexd config: FlexGate %s not found in file: %s\n",
						flexgate, AX_ROUTES_FILE);
				fclose(fp);
				fclose(fgt);
				return -1;
			} else {
				*digipath = '\0';
				for (k = 0; k < AX25_MAX_DIGIS; k++) {
					if (gw->digis[k] == NULL)
						break;
					strcat(digipath, " ");
					strcat(digipath, gw->digis[k]);
				}
                                if ((ax25_config_get_dev(gw->dev)) == NULL )
                                    sprintf(line, "%05d %-8s %5s %s\n",i++, gw->dest_call, gw->dev, digipath);
				else
                                    /* ax25 device */
                                    sprintf(line, "%05d %-8s %4s %s\n",i++, gw->dest_call, ax25_config_get_dev(gw->dev), digipath);
                                fputs(line, fgt);
			}
		}
	}
	fclose(fgt);
	fclose(fp);
return 0;
}

int download_dest(char *gateway, char *fname)
{
	FILE *tmp;
	static char *addr;
       	char port[14];
	char buffer[4096], path[AX25_MAX_DIGIS * 10];
	int buflen = 4096;
	char *commands[10], *dlist[9];		/* Destination + 8 digipeaters */
	fd_set read_fd;
	int paclen = 0;
	int window = 0;
	int n, cmd_send = 0, cmd_ack = 0, c, k;
	int s = 0;
	int addrlen = 0;
	int ret;
	unsigned int retlen;
	char *cp;
	struct sockaddr_rose rosebind;
	struct sockaddr_rose roseconnect;
	struct full_sockaddr_ax25 nrbind, nrconnect;

	union {
		struct full_sockaddr_ax25 ax25;
		struct sockaddr_rose  rose;
	} sockaddr;

	char digicall[10] = "\0";
	char destcall[10] = "\0";
	char destaddr[11] = "\0";
	static int af_mode = 0;

	memset(&sockaddr.rose, 0x00, sizeof(struct sockaddr_rose));
	memset(&rosebind, 0x00, sizeof(struct sockaddr_rose));
	memset(&roseconnect, 0x00, sizeof(struct sockaddr_rose));
	memset(&nrbind, 0x00, sizeof(struct full_sockaddr_ax25));
	memset(&nrconnect, 0x00, sizeof(struct sockaddr_ax25));

	gw = find_route(gateway, NULL);
	if (gw == NULL) {
		fprintf(stderr,"flexd connect: FlexGate %s not found in file: %s\n",
				gateway, FLEXD_CONF_FILE);
		return -1;
	} else {
		*path = '\0';
		for (k = 0; k < AX25_MAX_DIGIS; k++) {
			if (gw->digis[k][0] == '\0')
				dlist[k + 1] = NULL;
			else
				dlist[k + 1] = gw->digis[k];
		}
		dlist[0] = gw->dest_call;
		strcpy(port, gw->dev);
	}
	if (af_mode == 0) {

		if ((addr = ax25_config_get_addr(port)) == NULL) {
			nr_config_load_ports();

			if ((addr = nr_config_get_addr(port)) == NULL) {
				rs_config_load_ports();

				if ((addr = rs_config_get_addr(port)) == NULL) {
					fprintf(stderr,
						"flexd: invalid port setting\n");
					return -1;
				} else {
					af_mode = AF_ROSE;
				}
			} else {
				af_mode = AF_NETROM;
			}
		} else {
			af_mode = AF_AX25;
		}
	}

	switch (af_mode) {
	case AF_ROSE:
		paclen = rs_config_get_paclen(port);

		if (dlist[0] == NULL || dlist[1] == NULL) {
			fprintf(stderr,
				"flexd: too few arguments for Rose\n");
			return (-1);
		}
	/*
	 * Parse the passed values for correctness.
	 */
	roseconnect.srose_family = rosebind.srose_family = AF_ROSE;
	roseconnect.srose_ndigis = rosebind.srose_ndigis = 0;

	/*
	if (dlist[2] == NULL) {	*/
		strcpy(destaddr, dlist[1]);
		strcpy(destcall, dlist[0]);
/*		*digicall ='\0';*/
/*	}
	else {
		strcpy(destaddr, dlist[2]);
		strcpy(digicall, dlist[0]);
		strcpy(destcall, dlist[1]);
	} */

	if (ax25_aton_entry(destcall, roseconnect.srose_call.ax25_call) == -1) {
		sprintf(buffer, "ERROR: invalid destination callsign - %s\n", destcall);
		fprintf(stderr, "%s\n", buffer);
		return (-1);
	}

	if (rose_aton(destaddr, roseconnect.srose_addr.rose_addr) == -1) {
		sprintf(buffer, "ERROR: invalid destination Rose address - %s\n", destaddr);
		fprintf(stderr, "%s\n", buffer);
		return (-1);
	}
		rosebind.srose_family = AF_ROSE;
		roseconnect.srose_family = AF_ROSE;
		sockaddr.rose.srose_family = AF_ROSE;
		addrlen = sizeof(struct sockaddr_rose);
	/*
	 * Open the socket into the kernel.
	 */
	if ((s = socket(AF_ROSE, SOCK_SEQPACKET, 0)) < 0) {
		sprintf(buffer, "ERROR: cannot open Rose socket, %s\n", strerror(errno));
		fprintf(stderr, "%s\n", buffer);
		close(s);
		return (-1);
	}
	/*
	 * Set our AX.25 callsign and Rose address accordingly.
	 */
	if (rose_aton(addr, rosebind.srose_addr.rose_addr) == -1) {
		sprintf(buffer, "ERROR: invalid Rose port address - %s\n", addr);
		fprintf(stderr, "%s\n", buffer);
		close(s);
		return (-1);
	}

	if (ax25_aton_entry(mycall, rosebind.srose_call.ax25_call) == -1) {
		sprintf(buffer, "ERROR: invalid callsign - %s\n", mycall);
		fprintf(stderr, "%s\n", buffer);
		close(s);
		return (-1);
	}

	if (bind(s, (struct sockaddr *)&rosebind, addrlen) != 0) {
		sprintf(buffer, "ERROR: cannot bind Rose socket, %s\n", strerror(errno));
		fprintf(stderr, "%s\n", buffer);
		close(s);
		return (-1);
	}
	/*
	 * Lets try and connect to the far end.
	 */
	if (connect(s, (struct sockaddr *)&roseconnect, addrlen) != 0) {
		switch (errno) {
			case ECONNREFUSED:
				strcpy(buffer, "*** Flexd: Connection refused - aborting\n");
				break;
			case ENETUNREACH:
				strcpy(buffer, "*** Flexd: Route is closed - aborting\n");
				break;
			case EINTR:
				strcpy(buffer, "*** Flexd: Connection timed out - aborting\n");
				break;
			default:
				sprintf(buffer, "Flexd: ERROR cannot connect to Rose address, %s\n", strerror(errno));
				break;
		}
		fprintf(stderr, "%s\n", buffer);
		close(s);
		return (1);
	}
	break;
	case AF_NETROM:
		if (paclen == 0)
			paclen = nr_config_get_paclen(port);

		if (dlist[0] == NULL) {
			fprintf(stderr,
				"flexd: too few arguments for NET/ROM\n");
			return (-1);
		}
	/*
	 * Parse the passed values for correctness.
	 */
	nrconnect.fsa_ax25.sax25_family = AF_NETROM;
	nrbind.fsa_ax25.sax25_family = AF_NETROM;
	nrbind.fsa_ax25.sax25_ndigis    = 1;
	nrconnect.fsa_ax25.sax25_ndigis = 0;
	addrlen = sizeof(struct full_sockaddr_ax25);

	if (ax25_aton_entry(addr, nrbind.fsa_ax25.sax25_call.ax25_call) == -1) {
		sprintf(buffer, "ERROR: invalid NET/ROM port callsign - %s\n", addr);
		fprintf(stderr, "%s\n", buffer);
		return (-1);
	}

	if (ax25_aton_entry(mycall, nrbind.fsa_digipeater[0].ax25_call) == -1) {
		sprintf(buffer, "ERROR: invalid callsign - %s\n", mycall);
		fprintf(stderr, "%s\n", buffer);
		return (-1);
	}

/*	if (dlist[1] == NULL) { */
		strcpy(destcall, dlist[0]);
/*	}
	else {
		strcpy(destcall, dlist[1]);
		strcpy(digicall, dlist[0]);
	} */

        printf("destcall: '%s' digicall: '%s' mycall: '%s' port callsign: '%s'\n",
		destcall, digicall, mycall, addr);

	if (ax25_aton_entry(destcall, nrconnect.fsa_ax25.sax25_call.ax25_call) == -1) {
		sprintf(buffer, "ERROR: invalid destination callsign - %s\n", destcall);
		fprintf(stderr, "%s\n", buffer);
		return (-1);
		}
	/*
	 * Open the socket into the kernel.
	 */
	if ((s = socket(AF_NETROM, SOCK_SEQPACKET, 0)) < 0) {
		sprintf(buffer, "ERROR: cannot open NET/ROM socket, %s\n", strerror(errno));
		fprintf(stderr, "%s\n", buffer);
		close(s);
		return (-1);
	}

	/*
	 * Set our AX.25 callsign and NET/ROM callsign accordingly.
	 */
	if (bind(s, (struct sockaddr *)&nrbind, addrlen) != 0) {
		sprintf(buffer, "ERROR: cannot bind NET/ROM socket, %s\n", strerror(errno));
		fprintf(stderr, "%s\n", buffer);
		close(s);
		return (-1);
	}

	/*FSA*/
        sleep(1);
	/*FSA*/

	/*
	 * Lets try and connect to the far end.
	 */
	if (connect(s, (struct sockaddr *)&nrconnect, addrlen) != 0) {
		sprintf(buffer,"\nTrying %s ", destcall);
		switch (errno) {
			case ECONNREFUSED:
				strcat(buffer, "*** Flexd: Connection refused - aborting\n");
				break;
			case ENETUNREACH:
				strcat(buffer, "*** Flexd: Route is closed - aborting\n");
				break;
			case EINTR:
				strcat(buffer, "*** Flexd: Connection timed out - aborting\n");
				break;
			default:
				sprintf(buffer, "Flexd: ERROR cannot connect to NET/ROM node, %s\n", strerror(errno));
				break;
		}
		fprintf(stderr, "\nBUFFER:%s\n", buffer);
		close(s);
		return (1);
	}
	break;
	case AF_AX25:
		if (window == 0)
			window = ax25_config_get_window(port);
		if (paclen == 0)
			paclen = ax25_config_get_paclen(port);

		if (dlist[0] == NULL) {
			fprintf(stderr,
				"flexd: too few arguments for AX.25\n");
			return (-1);
		}
		if ((s = socket(AF_AX25, SOCK_SEQPACKET, 0)) < 0) {
			perror("flexd: socket");
			return (-1);
		}
		ax25_aton(ax25_config_get_addr(port), &sockaddr.ax25);
		if (sockaddr.ax25.fsa_ax25.sax25_ndigis == 0) {
			ax25_aton_entry(ax25_config_get_addr(port),
					sockaddr.ax25.fsa_digipeater[0].
					ax25_call);
			sockaddr.ax25.fsa_ax25.sax25_ndigis = 1;
		}
		sockaddr.ax25.fsa_ax25.sax25_family = AF_AX25;
		addrlen = sizeof(struct full_sockaddr_ax25);

		if (setsockopt
		    (s, SOL_AX25, AX25_WINDOW, &window,
		     sizeof(window)) == -1) {
			perror("flexd: AX25_WINDOW");
			close(s);
			return (-1);
		}
		if (setsockopt
		    (s, SOL_AX25, AX25_PACLEN, &paclen,
		     sizeof(paclen)) == -1) {
			perror("flexd: AX25_PACLEN");
			close(s);
			return (-1);
		}
		if (backoff != -1) {
			if (setsockopt
			    (s, SOL_AX25, AX25_BACKOFF, &backoff,
			     sizeof(backoff)) == -1) {
				perror("flexd: AX25_BACKOFF");
				close(s);
				return (-1);
			}
		}
		if (ax25mode != -1) {
			if (setsockopt
			    (s, SOL_AX25, AX25_EXTSEQ, &ax25mode,
			     sizeof(ax25mode)) == -1) {
				perror("flexd: AX25_EXTSEQ");
				close(s);
				return (-1);
			}
		}
		if (ax25_aton_arglist
		    ((const char **) dlist, &sockaddr.ax25) == -1) {
			close(s);
			return (-1);
		}
/* ATTENTION : Check if this is the right structure  ! */
		sockaddr.rose.srose_family = AF_AX25;
		addrlen = sizeof(struct full_sockaddr_ax25);
	/*
	 * Open the socket into the kernel.
	 */
	if ((s = socket(AF_AX25, SOCK_SEQPACKET, 0)) < 0) {
		sprintf(buffer, "flexd connect: cannot open AX.25 socket, %s\n",
				strerror(errno));
		write(STDOUT_FILENO, buffer, strlen(buffer));
		return (-1);
	}

	/*
	 * Set our AX.25 callsign and AX.25 port callsign accordingly.
	 */
	if (*mycall == '\0')
		sprintf(buffer, "\n%s %s\n", addr, addr);
	else
		sprintf(buffer, "\n%s %s\n", mycall, addr);

	ax25_aton(buffer, &sockaddr.ax25);
	sockaddr.ax25.fsa_ax25.sax25_family = AF_AX25;
	addrlen = sizeof(struct full_sockaddr_ax25);

	if (bind(s, (struct sockaddr *) &sockaddr, addrlen) != 0) {
		sprintf(buffer, "flexd connect: cannot bind AX.25 socket, %s\n",
				strerror(errno));
		write(STDOUT_FILENO, buffer, strlen(buffer));
		close(s);
		return (-1);
	}
	/*FSA*/
        sleep(1);
	/*FSA*/
	/*
	 * Lets try and connect to the far end.
	 *
	 */
	if (fcntl(s, F_SETFL, O_NONBLOCK) == -1) {
		sprintf(buffer, "flexd connect: fcntl on socket: %s\n",
				strerror(errno));
		write(STDOUT_FILENO, buffer, strlen(buffer));
		close(s);
		return (-1);
	}

	if (ax25_aton_arglist((const char **) dlist, &sockaddr.ax25) == -1) {
		sprintf(buffer,
				"flexd connect: invalid destination callsign or digipeater\n");
		write(STDOUT_FILENO, buffer, strlen(buffer));
		close(s);
		return (-1);
	}

	/*FSA*/
        sleep(1);
	/*FSA*/
/**/
        if (connect(s, (struct sockaddr *) &sockaddr, addrlen) == -1
		&& errno != EINPROGRESS) {
		switch (errno) {
		case ECONNREFUSED:
			strcpy(buffer, "*** Flexd: Connection refused - aborting\n");
			break;
		case ENETUNREACH:
			strcpy(buffer, "*** Flexd: Route is closed - aborting\n");
			break;
		case EINTR:
			strcpy(buffer, "*** Flexd: Connection timed out - aborting\n");
			break;
		default:
			sprintf(buffer, "*** Flexd: Cannot connect, %s\n", strerror(errno));
			break;
		}

		write(STDOUT_FILENO, buffer, strlen(buffer));
		close(s);
		return (1);
	}
/**/
	break;
	}

	fflush(stdout);
/*
 * We got there.
 */
	while (1) {
		FD_ZERO(&read_fd);
		FD_SET(s, &read_fd);
		if (select(s + 1, &read_fd, NULL, 0, 0) == -1) {
                    break;
		}
		if (FD_ISSET(s, &read_fd)) {
                    /*	See if we got connected or if this was an error		*/
                    getsockopt(s, SOL_SOCKET, SO_ERROR, &ret, &retlen);

                    switch (af_mode) {
                        case AF_ROSE:
                            break;
                        case AF_NETROM:
                            break;
                        case AF_AX25: {
/*FSA don'know how but works!*/
                            if ((ret != 0) && (ret != 256)) {
                                cp = strdup(strerror(ret));
                                strlwr(cp);
                                sprintf(buffer, "flexd connect: Failure with %s error %d %s\n",
						gateway, ret, cp);
                                write(STDOUT_FILENO, buffer, strlen(buffer));
                                free(cp);
                                close(s);
                                return 1;
                            }
                        } break;
                        default:
                            break;
                    }
                    break;
		}
	}

	commands[0] = "d\r";
	commands[1] = "q\r";
	commands[2] = "b\r";
	commands[3] = NULL;

	/*
	 * Loop until one end of the connection goes away.
	 */

	if ((tmp = fopen(fname, "w")) == NULL) {
		fprintf(stderr, "flexd connect: Cannot open temporary file: %s\n",
				fname);
		close(s);
		return (-1);
	}
/*FSA*/
        sleep(1);
/*FSA*/
	for (;;) {
		FD_ZERO(&read_fd);
		FD_SET(s, &read_fd);
		if (select(s + 1, &read_fd, NULL, NULL, NULL) == -1) {
			break;
		}
		if (FD_ISSET(s, &read_fd)) {
			if ((n = read(s, buffer, 512)) == -1)
				break;
			for (c = 0; c < n; c++) {
				if (buffer[c] == '\r')
					buffer[c] = '\n';
				if ((buffer[c] == '=' || buffer[c] == '-') && c < n - 1 && buffer[c + 1] == '>') {
					cmd_ack++;
				}
			}
			if (cmd_send == 1) {
				fwrite(buffer, sizeof(char), n, tmp);
			}
		}

		if (cmd_ack != 0) {
			if (commands[cmd_send] != NULL) {
				write(s, commands[cmd_send], 2);
				cmd_send++;
			}
			cmd_ack = 0;
		}
	}
	close(s);
	fclose(tmp);
	return 0;
}

int parse_dest(char *gateway, char *fname)
{
	FILE *fdst, *tmp;
	char *call, *ssid, *rtt, *cp, buf[1024], line[1024];
	if ((tmp = fopen(fname, "r")) == NULL) {
		fprintf(stderr, "flexd update: Cannot open temporary file: %s\n",
				fname);
		return (-1);
	}

	if ((fdst = fopen(FLEX_DST_FILE, "w")) == NULL) {
		fprintf(stderr,
				"flexd update: Cannot open flexnet destinations file: %s\n",
				FLEX_DST_FILE);
		fclose(tmp);
		return (-1);
	}

	fputs("callsign  ssid     rtt    gateway\n", fdst);

	while (fgets(buf, sizeof(buf), tmp)) {
		cp = strtok(buf, " \t\n\r");
/*		if (cp == NULL || i++ < 2)fprintf(stderr, */
		if (cp == NULL)
			continue;			/* empty line/connect text */
		if (*cp == '#' || *cp == '=' || *cp == ' ' || *cp == '*'
			|| *cp == '-')
			continue;			/* comment line/system prompt */
		if (strncmp(cp, "73!", 3) == 0)
			continue;			/* End greeting */

		if (strncmp(cp, "Cmd:", 4) == 0)	/* AWZnode prompt */
			continue;

		if ((strncmp(cp, "Connected to", 12) == 0)
                        || (strncmp(cp, "=>", 2) == 0))	/* Flexnode prompt */
			continue;

		/* CALL SSID-ESID RTT */
		do {
			call = cp;
			if (call == NULL)
				break;
			ssid = strtok(NULL, " \t\n\r");
			if (ssid == NULL)
				break;
			rtt = strtok(NULL, " \t\n\r");
			if (rtt == NULL)
				break;
			sprintf(line, "%-8s  %-5s %6d    %05d\n", call, ssid,
					safe_atoi(rtt), 0);
/*				safe_atoi(rtt), gateway); */
			fputs(line, fdst);
			cp = strtok(NULL, " \t\n\r");
		} while (cp != NULL);
	}
	fclose(fdst);
	fclose(tmp);

	return 0;
}

int update_flex(void)
{
	int ret;
	char fname[80];
	sprintf(fname, "%s.session.%s", FLEXD_TEMP_PATH, flexgate);

	if ((ret = download_dest(flexgate, fname)) == 0) {
		parse_dest(flexgate, fname);
		remove(fname);
	}
	return (ret);
}

void hup_handler(int sig)
{
	int i;
	signal(SIGHUP, SIG_IGN);
	i = read_conf();

	signal(SIGHUP, hup_handler);
}

void alarm_handler(int sig)
{
	signal(SIGALRM, SIG_IGN);
	update_flex();

	signal(SIGALRM, alarm_handler);		/* Restore alarm handler */
	alarm(poll_time);
}

static void process_options(int argc, char *argv[])
{
	int c;

	do {
		c = getopt(argc, argv, "?hdvxp:");
		switch (c) {
		case 'h':
		case '?':
			Usage();
			break;
		case 'd':
			fprintf(stderr, "Foreground mode\n");
			is_daemon = 0;
			verbose = 1;
			break;
		case 'x':
			fprintf(stderr, "Debug mode\n");
			debug = 1;
			break;
		case 'v':
			fprintf(stderr, "Verbose mode\n");
			verbose = TRUE;
			break;
		case 'p':
			fprintf(stderr, "Passive mode\n");
			passive = 1;
			break;
		default :
			break;
		}
	} while (c != EOF);
}

int main(int argc, char *argv[])
{
	int i;
	signal(SIGPIPE, SIG_IGN);

	openlog("flexd", LOG_ERR , LOG_INFO);
	syslog(LOG_WARNING, "Starting flexd");

	if (ax25_config_load_ports() == 0) {
		fprintf(stderr, "flexd error: No AX25 port data configured\n");
		return 1;
	}

	process_options(argc, argv);

	if ((i = read_conf()) == -1)
		return 1;

	if ((is_daemon) && (!daemon_start(TRUE)) ) {
		fprintf(stderr, "flexd: cannot become a daemon\n");
		return 1;
	}

	if ((i = update_flex()) == -1) {
		fprintf(stderr, "\nStopping application. Restart flexd after changing the configuration\n");
		signal(SIGKILL, hup_handler);
		return (i);
	}

	signal(SIGHUP, hup_handler);
	signal(SIGALRM, alarm_handler);
	alarm(poll_time);

	for (;;)
		pause();

	return 0;
}
