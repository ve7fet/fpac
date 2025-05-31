
/******************************************************
 * wp/wpedit.c                                        *
 * FPAC project.            FPAC WP EDITION           *
 *                                                    *
 * Parts of code from different sources of ax25-utils *
 *                                                    *
 * F6FBB 05-1997                                      *
 *                                                    *
 ******************************************************/

/******************************************************
 * 12/05/97 1.00 F6FBB First draft !
 * 07/09/97 1.01 F1OAT fpacwpd mode
 * 09/11/2006 status display modified
 *		menu N toggles node type of record
 * 09/10/2009 if digi deleted, delete city locator and name 
 * 22/05/2012 set date and time when creating a new record
 * 05/10/2024 return was incorrect after address entry
 ******************************************************/
 
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <ctype.h>

#include <time.h>
/*#include <sys/types.h>*/
/*#include <sys/fcntl.h>*/
#include <sys/file.h>
/*#include <sys/stat.h>*/
#include <sys/ioctl.h>
/*#include <sys/socket.h>*/

#include "ax25compat.h"

#include "wp.h"

int verbose = 0;
int cr = 0;

/*** Prototypes *******************/
static void readline(char *, int);

#define CR() printf( (cr) ? "\r" : "\n");
 
void readline(char *buf, int len)
{
	int nb;
	
	nb = read(STDIN_FILENO, buf, len-1);
	if (nb <= 0)
		exit(1);

	buf[nb] = '\0';
}

int main(int ac, char **av)
{
	int p; /* i, len, num, ret; */
	int rep;
	int end = 0;
	int error = 0;
	char line[80];
	char command[80];
	char str[80]; /* dnic[5]; */
	char *call, *pt; /* *add; */
	ax25_address addr;
	wp_t wp;
	time_t present_time;

	if (wp_open("WPEDIT-0")) {
		perror("Cannot open WP service");
		exit(1);
	}
		
	while ((p = getopt(ac, av, "vcl:")) != -1) 
	{
		switch (p)
		{
		case 'v' :
			printf("Verbose is set !\n");
			verbose = 1;
			break;
		case 'c' :
			cr = 1;
			break;
		case '?' :
			fprintf(stderr, "usage: wpedit [-c] callsign\n");
			return(1);
		}
	}

	if (optind == ac)
	{
		printf("Callsign is missing");
		CR();
		printf("usage: wpedit [-c] [-v] callsign\n");
		CR();
		return(1);
	}

	call = strupr(av[optind]);
	
	/* If there is no SSID appended to the callsign
	then we append a -0 on the end of the call */
	if (!strchr(call, '-'))
		strcat(call, "-0");

	if (ax25_aton_entry(call, addr.ax25_call) != 0)
	{
		printf("Invalid callsign %s", call); CR();
		return(1);
	}

	memset(&wp, 0, sizeof(wp_t));
	present_time = time(NULL);
	p = wp_get(&addr, &wp) ;
	
	if(p != 0) 
	{
		for (;;) 
		{
			CR();
			printf("Callsign %s not in database. Create [Y-N] ? ", call);
			fflush(stdout);
			readline(line, sizeof(line));

			rep = toupper(line[0]);

			if ((rep == 'Y') || (rep == 'N'))
				break;
		}

		if (rep == 'N')
			return(0);

		wp.address.srose_call = addr;
		wp.date = present_time;
	}

	CR();

	while (!end)
	{
		int i;
		int len;
		int num;
		int ret;
	/*	char *p;*/
		char *add;
		char dnic[5];
		char buf[20];

		printf("Callsign  Status  Last update       DNIC address  Type  digis Name City Locator"); CR();

		add = rose_ntoa(&wp.address.srose_addr);
		strncpy(dnic, add, 4); dnic[4] = '\0';

		printf("%-9s", call);

		if (wp.is_deleted == 1)
			printf(" deleted ");
		else
			printf("         ");

 		my_date(buf, wp.date),
 		
		printf("%s => %s %s  ", 
 			buf,
			dnic, 
			add+4);

		if (wp.is_node == 1)
			printf(" node ");
		else
			printf(" user ");

		for (i = 0 ; i < wp.address.srose_ndigis ; i++)
			printf("%-9s ", ax25_ntoa(&wp.address.srose_digis[i]));

		printf("%s %s %s", wp.name, wp.city, wp.locator);
		CR(); CR();

		printf("WP-Editing %s : (A)ddress (D)igi (N) Toggles node attribute (R)emove (U)ndelete (B)ye", call); CR();
		fflush(stdout);

		readline(line, sizeof(line));

		command[0] = str[0] = '\0';

		sscanf(line, "%s %[^\r\n]", command, str);
		
/* F6BVP record is set to present date
		wp.date = time (NULL); */

		switch(toupper(command[0]))
		{
		case 'A':
			len = strlen(str);
			if (len == 0)
				error = 1;
			else if (len != 10)
				error = 2;
			else if (strcspn(str, "0123456789"))
				error = 3;
			else
			{
				rose_aton(str, wp.address.srose_addr.rose_addr);
if (verbose) syslog(LOG_INFO, "rose_aton passé");

		add = rose_ntoa(&wp.address.srose_addr);
		strncpy(dnic, add, 4); dnic[4] = '\0';

		printf("%-9s", call);
		printf("%s => %s %s  ", 
 			buf,
			dnic, 
			add+4);
		CR(); CR();

if (verbose) syslog(LOG_INFO, "calling wp_set()");

				ret = wp_set(&wp);

if (verbose) syslog(LOG_INFO, "retour wp_set() : %d", ret);

				if (ret == 0)
					fprintf(stdout, "WP record '%s' updated", call);
				else	
					fprintf(stderr, "*** Error in address setting"); 
				CR(); CR();

if (verbose) syslog(LOG_INFO, "calling wp_get()");

				wp_get(&wp.address.srose_call, &wp);
if (verbose) syslog(LOG_INFO, "retour wp_get()");

			}
			break;
		case 'B':
			end = 1;
			break;
		case 'D':
			if (*str)
			{
				num = 0;
				pt = strtok(str, " ,");
				
				if (strcmp(pt, ".") == 0)
				{
					pt = NULL;
					wp.name[0] = '\0';
					wp.city[0] = '\0';
					wp.locator[0] = '\0';
				}
				else
				{
					while (pt)
					{
						if (num == ROSE_MAX_DIGIS)
						{
							printf("*** Error: too many digis (max %d)",ROSE_MAX_DIGIS); 
							CR(); CR();
							break;
						}
						if (ax25_aton_entry(pt, wp.address.srose_digis[num].ax25_call) != 0)
						{
							printf("*** Error: invalid callsign %s", str); 
							CR(); CR();
							break;
						}
						pt = strtok(NULL, " ,");
						++num;
					}
				}
		
				if (pt == NULL)
				{
					printf("PLEASE WAIT !"); CR(); CR();

					wp.address.srose_ndigis = num;

					ret = wp_set(&wp);
					
					if (verbose) printf("Ret : %d\n", ret);
					
					if (ret < 0)
						printf("WP record '%s' updated", call);
					else
						printf("*** Error in list of digis");
				       	CR(); CR();
					wp_get(&wp.address.srose_call, &wp);
				}
			}
			else
			{
				printf("Type \"D .\" to remove all digis"); CR(); CR();
			}
			break;
		case 'N':
			if (wp.is_node == 1)
				wp.is_node = 0;
			else 
				wp.is_node = 1;
			ret = wp_set(&wp);
			if (ret == 0)
				printf("Node '%s' attribute updated", call);
			else 
				printf("record '%s' not updated - error %d", call, ret);
			CR(); CR();
			wp_get(&wp.address.srose_call, &wp);
			break;
		case 'R':
			wp.is_deleted = 1;
			if (wp_set(&wp) == 0)
				printf("WP record '%s' deleted", call);
			else
				printf("record '%s' not updated", call);
			CR(); CR();
			wp_get(&wp.address.srose_call, &wp);
			break;
		case 'U':
			wp.is_deleted = 0;
if (verbose) syslog(LOG_INFO,"calling wp_set()");
			if (wp_set(&wp) == 0)
				printf("WP record '%s' restored", call);
			else
				printf("record '%s' not updated", call);
if (verbose) syslog(LOG_INFO, "retour wp_set()");
			CR(); CR();
if (verbose) syslog(LOG_INFO,"calling wp_get()");
			wp_get(&wp.address.srose_call, &wp);
if (verbose) syslog(LOG_INFO, "retour wp_get()");

			break;
		default :
			printf("Unknown command %s", line); CR(); CR();
			break;
		}

		if (error)
		{
			printf("*** Error: ");
			switch(error)
			{
			case 1:
				printf("Address missing");
				break;
			case 2:
				printf("Address must have 10 digits");
				break;
			case 3:
				printf("Address is only digits");
				break;
			}

			error = 0;
			CR(); CR();
		}

	}

CR(); CR();

return(0);
}

