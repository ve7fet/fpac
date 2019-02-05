

/*******************************
 *
 * calld.c  (from ax25_call.c)
 *
 *******************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>

#include <time.h>
/*#include <sys/types.h>*/
#include <sys/ioctl.h>
/*#include <sys/socket.h>*/

#include "ax25compat.h"

#include "fpac.h"
#include "wp.h"

union sockaddr_ham
{
	struct full_sockaddr_ax25 axaddr;
	struct full_sockaddr_rose rsaddr;
};

#define BUFLEN 4096

cfg_t cfg;

void alarm_handler(int sig)
{
}

void err(char *message)
{
	if ((write(STDOUT_FILENO, message, strlen(message))) < 0)
	{
                if (errno)
                        perror("FPAC write error:");
        }
	sleep(1);
	exit(1);
}

static char *special_calls(char *ptr)
{
	static char str[256];
	char desti[256];
	
	/* If there is a digi, don't check ... */
	if (sscanf(ptr, "%s %s", desti, str) == 2)
		return ptr;
		
	/* Node callsign */
	if	(strcasecmp(desti, cfg.alt_callsign) == 0)
	{
		sprintf(str, "%s %s", desti, cfg.address);
		return str;
	}

	{
		alias_t *p;
		for (p = cfg.alias ; p ; p = p->next)
		{
			if	(strcasecmp(desti, p->alias) == 0)
			{
				strcpy(str, p->path);
				return str;
			}
		}
	}

	{
		appli_t *p;
		for (p = cfg.appli ; p ; p = p->next)
		{
			if	(strcasecmp(desti, p->call) == 0)
			{
				sprintf(str, "%s %s", desti, cfg.address);
				return str;
			}
		}
	}

	{
		luser_t *p;
		for (p = cfg.luser ; p ; p = p->next)
		{
			if	(strcasecmp(desti, p->call) == 0)
			{
				sprintf(str, "%s %s", desti, cfg.address);
				return str;
			}
		}
	}

	{
		/* White pages */
		struct full_sockaddr_rose wpaddr;
		ax25_address call;

		if (ax25_aton_entry(desti, call.ax25_call) != -1)
		{
			int ok = 0;
			
			if (wp_open("CALLD") >= 0)
			{
				ok = (wp_search(&call, &wpaddr) == 0);
				wp_close();
			}
			
			if (ok)
			{
				sprintf(str, "*** WP routing via %s\r", fpac2asc(&wpaddr.srose_addr));
				if ((write(STDOUT_FILENO, str, strlen(str))) < 0)
				{
         		       		if (errno)
                        		perror("FPAC write error:");
        			}
				sprintf(str, "%s %s", desti, rose_ntoa(&wpaddr.srose_addr));
				return str;
			}
		}
	}
	return ptr;
}

/* Transforms a ROSE connection string to struct full_sockaddr_rose */
static int rs_ax25_aton (char *address, struct full_sockaddr_rose *addr)
{
  char *command, *call, *dnic, *rose, *ptr;
  char roseaddr[12];
  
  command = strdup(address);
  if (command)
  {

    call = strtok (command, " \t\n\r");
    dnic = strtok (NULL, " ,\t\r\n");
    if (strncasecmp(dnic, "VIA", strlen(dnic)) == 0)
        dnic = strtok (NULL, " ,\t\r\n");
	if (strlen(dnic) == 4)
	{
		rose = strtok (NULL, " ,\t\r\n");
	}
	else if (strlen(dnic) == 6)
	{
		rose = dnic;
		dnic = cfg.dnic;
	}
	else if (strlen(dnic) == 10)
	{
		rose = dnic+4;
	}
	else
	{
        free(command);
        return -1;
	}

	memcpy(roseaddr, dnic, 4);
	memcpy(roseaddr+4, rose, 6);
	roseaddr[10] = '\0';

    addr->srose_family = AF_ROSE;
    addr->srose_ndigis = 0;

    if (ax25_aton_entry (call, addr->srose_call.ax25_call) == -1)
	{
		free(command);
		return -1;
	}
  
    if (rose_aton (roseaddr, addr->srose_addr.rose_addr) == -1)
	{
		free(command);
		return -1;
	}

	/* Digis... */
	ptr = strtok (NULL, " ,\t\r\n");
	while (ptr)
	{
		if (ax25_aton_entry (ptr, addr->srose_digis[addr->srose_ndigis].ax25_call) == -1)
		{
			free(command);
			return -1;
		}
		addr->srose_ndigis++;  
		ptr = strtok (NULL, " ,\t\r\n");
	}
	
    free(command);
  }
  return sizeof(struct full_sockaddr_rose);
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
		desc = "Invalid AX.25 Facility Requested";
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

int is_rose(char *address)
{
	int i;

	for (i = 0 ; *address ; i++, address++)
		if (!isdigit(*address))
			break;
	return(i == 4 || i == 6 || i == 10);
}

int main(int argc, char **argv)
{
	int buflen;
	int type;
	char address[256];
	char *ptr;
	char *sender;
	char buffer[4096], *addr;
	fd_set read_fd;
	int nb, offs;
	int n, s = -1, bindlen = 0, addrlen = 0;
	union sockaddr_ham axbind, axconnect;

	/*
	 * Arguments should be "ax25_call port"
	 */
	if (argc < 2) {
		err("ERROR: invalid number of parameters\r");
	}

	if (ax25_config_load_ports() == 0) {
		err("ERROR: problem with axports file\r");
	}

	if (rs_config_load_ports() == 0) {
		err("ERROR: problem with axports file\r");
	}

	/* Read FPAC configuration file */
	if (cfg_open(&cfg) != 0)
	{
		err("ERROR: problem with fpac.conf file\r");
	}

	/* Ask for callsign */

	printf("Callsign : ");
	fflush(stdout);

	offs = 0;
	while (offs < 256)
	{
		nb = read(0, buffer+offs, 256-offs);
		if (nb <= 0)
		{
			/* Disconnected or problem ! */
			return(0);
		}
		offs += nb;
		buffer[offs] = '\0';
		if (strchr(buffer, '\r'))
			break;
	}

	/* Deletes the '\r' */
	if (buffer[offs-1] == '\r')
		buffer[offs-1] = '\0';

	/* extract sender */
	ptr = strchr(buffer, '^');
	if (ptr)
		*ptr++ = '\0';

	/* Deletes the '.' for binary information */
	sender = buffer;
	if (*sender == '.')
		++sender;

	sprintf(address, "*** Connecting %s\r", ptr);
	if ((write(STDOUT_FILENO, address, strlen(address))) < 0)
	{
                if (errno)
                        perror("FPAC write error:");
        }

	/*
	 *
	 * Calling callsign    = sender
	 * Destination + digis = ptr
	 *
	 */

	/* Check special callsigns */
	ptr = special_calls( ptr);

	/* Check the kind of connection : ax25 or ROSE */
	sscanf( ptr, "%*s %s", address);
	if (strncasecmp(address, "VIA", strlen(address)) == 0)
		sscanf( ptr, "%*s %*s %s", address);

	if (is_rose(address))
		type = AF_ROSE;
	else
		type = AF_AX25;

	/*
	 * Parse the passed values for correctness.
	 */

	switch (type)
	{

	case AF_AX25:
		axbind.axaddr.fsa_ax25.sax25_family = AF_AX25;
		axbind.axaddr.fsa_ax25.sax25_ndigis = 1;

		if (ax25_aton_entry(sender, axbind.axaddr.fsa_ax25.sax25_call.ax25_call) == -1) {
			sprintf(buffer, "ERROR: invalid callsign - %s\r", sender);
			err(buffer);
		}

		if ((addr = ax25_config_get_addr(argv[1])) == NULL) {
			sprintf(buffer, "ERROR: invalid AX.25 port name - %s\r", argv[1]);
			err(buffer);
		}

		if (ax25_aton_entry(addr, axbind.axaddr.fsa_digipeater[0].ax25_call) == -1) {
			sprintf(buffer, "ERROR: invalid AX.25 port callsign - %s\r", argv[1]);
			err(buffer);
		}

		/* Indicatif appele et digis... */
		if ((addrlen = ax25_aton (ptr, &axconnect.axaddr)) == -1)
		{
			fprintf (stderr, "Unknown callsign %s\n", ptr);
			return 0;
		}

		/*
		 * Open the socket into the kernel.
		 */
		if ((s = socket(AF_AX25, SOCK_SEQPACKET, 0)) < 0) {
			sprintf(buffer, "ERROR: cannot open AX.25 socket, %s\r", strerror(errno));
			err(buffer);
		}

		bindlen = sizeof(struct full_sockaddr_ax25);

		break;

	case AF_ROSE:
		axbind.rsaddr.srose_family = AF_ROSE;
		axbind.rsaddr.srose_ndigis = 0;

		if (ax25_aton_entry(sender, axbind.rsaddr.srose_call.ax25_call) == -1) {
			sprintf(buffer, "ERROR: invalid callsign - %s\r", sender);
			err(buffer);
		}

		/* Port */
		addr = rs_config_get_addr (NULL);
		rose_aton (addr, axbind.rsaddr.srose_addr.rose_addr);

		if ((addrlen = rs_ax25_aton(ptr, &axconnect.rsaddr)) == -1)
		{
			fprintf (stderr, "Unknown callsign %s\n", ptr);
			return 0;
		}

		/*
		 * Open the socket into the kernel.
		 */
		if ((s = socket(AF_ROSE, SOCK_SEQPACKET, 0)) < 0) {
			sprintf(buffer, "ERROR: cannot open ROSE socket, %s\r", strerror(errno));
			err(buffer);
		}

		bindlen = sizeof(struct full_sockaddr_rose);

		break;
	}

	buflen = BUFLEN;
	setsockopt(s, SOL_SOCKET, SO_RCVBUF, &buflen, sizeof(buflen));
	setsockopt(s, SOL_SOCKET, SO_SNDBUF, &buflen, sizeof(buflen));

	/*
	 * Set our AX.25 callsign and AX.25 port callsign accordingly.
	 */
	if (bind(s, (struct sockaddr *)&axbind, addrlen) != 0) {
		sprintf(buffer, "ERROR: cannot bind AX.25 socket, %s\r", strerror(errno));
		err(buffer);
	}

	/*
	 * If no response in 300 seconds, go away.
	 */
	alarm(300);

	signal(SIGALRM, alarm_handler);

	/*
	 * Lets try and connect to the far end.
	 */
	if (connect(s, (struct sockaddr *)&axconnect, addrlen) != 0) {

		if (type == AF_ROSE)
		{
			char origin[80];
			struct rose_facilities_struct facilities;
			struct rose_cause_struct rose_cause;
			
			rose_cause.cause      = ROSE_LOCAL_PROCEDURE;
			rose_cause.diagnostic = 0x12;

			ioctl(s, SIOCRSGCAUSE, &rose_cause);

			origin[0] = '\0';
			if ((ioctl(s, SIOCRSGFACILITIES, &facilities) != -1) &&
				(facilities.fail_call.ax25_call[0]))
			{
				sprintf(origin, " at %s @%s",
					ax25_ntoa(&facilities.fail_call),
					rose_ntoa(&facilities.fail_addr));
			}

			sprintf(address, "*** No answer from %s%s\r*** %02X%02X - %s\r",
				ptr,
				origin,
				rose_cause.cause, 
				rose_cause.diagnostic, 
				reason(rose_cause.cause, rose_cause.diagnostic));
		}
		else
		{
			switch (errno) {
			case ECONNREFUSED:
				strcpy(address, "*** Connection refused - aborting\r");
				break;
			case ENETUNREACH:
				strcpy(address, "*** No known route - aborting\r");
				break;
			case EINTR:
				strcpy(address, "*** Connection timed out - aborting\r");
				break;
			default:
				sprintf(address, "*** Cannot connect (%s)\r", strerror(errno));
				break;
			}
		}
		err(address);
	}

	/*
	 * We got there.
	 */
	alarm(0);

	sprintf(buffer, "*** Connection done\r");
	if ((write(STDOUT_FILENO, buffer, strlen(buffer))) < 0)
	{
                if (errno)
                        perror("FPAC write error:");
        }

	/*
	 * Loop until one end of the connection goes away.
	 */
	for (;;) {
		FD_ZERO(&read_fd);
		FD_SET(STDIN_FILENO, &read_fd);
		FD_SET(s, &read_fd);
		
		select(s + 1, &read_fd, NULL, NULL, NULL);

		if (FD_ISSET(s, &read_fd)) 
		{
			if ((n = read(s, buffer, sizeof(buffer))) <= 0) 
			{
				if (type == AF_ROSE)
				{
					int ret;
					char origin[80];
					struct rose_facilities_struct facilities;
					struct rose_cause_struct rose_cause;
					rose_cause.cause      = ROSE_LOCAL_PROCEDURE;
					rose_cause.diagnostic = 0x12;

					ioctl(s, SIOCRSGCAUSE, &rose_cause);

					origin[0] = '\0';
					ret = ioctl(s, SIOCRSGFACILITIES, &facilities);
					if ((ret != -1) && (facilities.fail_call.ax25_call[0]))
					{
						sprintf(origin, " at %s @ %s",
							ax25_ntoa(&facilities.fail_call),
							fpac2asc(&facilities.fail_addr));
					}

					sprintf(address, "\r*** Disconnected%s\r*** %02X%02X - %s\r",
						origin,
						rose_cause.cause, 
						rose_cause.diagnostic, 
						reason(rose_cause.cause, rose_cause.diagnostic));
				}
				else	
					strcpy(address, "\r*** Disconnected\r");
				err(address);
			}
			if ((write(STDOUT_FILENO, buffer, n)) < 0)
			{
                		if (errno)
                        	perror("FPAC write error:");
        		}
		}

		if (FD_ISSET(STDIN_FILENO, &read_fd)) 
		{
			if ((n = read(STDIN_FILENO, buffer, ROSE_MTU)) <= 0) 
			{
				if (type == AF_ROSE)
				{
					int ret;
					char origin[80];
					struct rose_facilities_struct facilities;
					struct rose_cause_struct rose_cause;
					
					rose_cause.cause      = ROSE_LOCAL_PROCEDURE;
					rose_cause.diagnostic = 0x12;

					ioctl(s, SIOCRSGCAUSE, &rose_cause);

					origin[0] = '\0';
					ret = ioctl(s, SIOCRSGFACILITIES, &facilities);
					if ((ret != -1) && (facilities.fail_call.ax25_call[0]))
					{
						sprintf(origin, " at %s @ %s",
							ax25_ntoa(&facilities.fail_call),
							fpac2asc(&facilities.fail_addr));
					}

					sprintf(address, "\r*** Disconnected%s\r*** %02X%02X - %s\r",
						origin,
						rose_cause.cause, 
						rose_cause.diagnostic, 
						reason(rose_cause.cause, rose_cause.diagnostic));
				}
				else	
					strcpy(address, "\r*** Disconnected\r");
				err(address);
			}
			if ((write(s, buffer, n)) < 0)
			{
                		if (errno)
                        	perror("FPAC write error:");
        		}
		}
	}

	return 0;
}
