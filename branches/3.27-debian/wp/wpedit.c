
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

int cr = 0;

/*** Prototypes *******************/
static char *my_date(time_t date);
static void readline(char *, int);

#define CR() printf( (cr) ? "\r" : "\n"); 

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

	if (wp_open("WPEDIT-0")) {
		perror("Cannot open WP service");
		exit(1);
	}
	
	while ((p = getopt(ac, av, "cl:")) != -1) 
	{
		switch (p)
		{
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
		return(1);
	}

	call = strupr(av[optind]);

	if (ax25_aton_entry(call, addr.ax25_call) != 0)
	{
		printf("Invalid callsign %s", call); CR();
		return(1);
	}

	memset(&wp, 0, sizeof(wp_t));

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

		printf("Callsign  Status  Last update       DNIC address  Type  digis Name City Locator"); CR();

		add = rose_ntoa(&wp.address.srose_addr);
		strncpy(dnic, add, 4); dnic[4] = '\0';

		printf("%-9s", call);

		if (wp.is_deleted == 1)
			printf(" deleted ");
		else
			printf("         ");

 		printf("%s => %s %s  ", 
			my_date(wp.date), 
			dnic, 
			add+4);

		if (wp.is_node == 1)
			printf(" node ");
		else
			printf(" user ");

		for (i = 0 ; i < wp.address.srose_ndigis ; i++)
			printf("%s ", ax25_ntoa(&wp.address.srose_digis[i]));

		printf("%s %s %s", wp.name, wp.city, wp.locator);
		CR(); CR();

		printf("WP-Editing %s : (A)ddress (D)igi (N) Toggles node attribute (R)emove (U)ndelete (B)ye", call); CR();
		fflush(stdout);

		readline(line, sizeof(line));

		command[0] = str[0] = '\0';

		sscanf(line, "%s %[^\r\n]", command, str);

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
				wp_set(&wp);
				printf("%s WP record updated", call); CR(); CR();
				wp_get(&wp.address.srose_call, &wp);
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
					wp.address.srose_ndigis = num;
					ret = wp_set(&wp);
					if (ret == 0)
					{
						printf("%s WP record updated", call); CR(); CR();
					}
					else
					{
						printf("*** Error in list of digis"); CR(); CR();
					}
					wp_get(&wp.address.srose_call, &wp);
				}
			}
			else
			{
				printf("Type \"D .\" to remove all digis"); CR(); CR();
			}
			break;
		case 'N':
			if(wp.is_node == 1)
				wp.is_node = 0;
			else
				wp.is_node = 1;
			if (wp_set(&wp))
				printf("%s Node attribute updated", call);
			else
				printf("%s record not updated", call);
			CR(); CR();
			wp_get(&wp.address.srose_call, &wp);
			break;
		case 'R':
			wp.is_deleted = 1;
			if (wp_set(&wp))
				printf("%s WP record deleted", call);
			else
				printf("%s record not updated", call);
			CR(); CR();
			wp_get(&wp.address.srose_call, &wp);
			break;
		case 'U':
			wp.is_deleted = 0;
			if (wp_set(&wp))
				printf("%s WP record restored", call);
			else
				printf("%s record not updated", call);
			CR(); CR();
			wp_get(&wp.address.srose_call, &wp);
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

	return(0);
}

static char *my_date(time_t date)
{
	static char buf[20];
	struct tm *sdate;

	sdate = localtime (&date);
	sprintf(buf, "%02d/%02d/%02d %02d:%02d", 
		sdate->tm_mday,
		sdate->tm_mon + 1, 
		sdate->tm_year%100,
		sdate->tm_hour,
		sdate->tm_min);
	return(buf);
}

void readline(char *buf, int len)
{
	int nb;
	
	nb = read(STDIN_FILENO, buf, len-1);
	if (nb <= 0)
		exit(1);

	buf[nb] = '\0';
}
