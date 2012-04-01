
/********************************************************
 * wp/wplist.c                                       	*
 * FPAC project.            FPAC WP LIST             	*
 * list WP data base records local Node memory image	*
 * derived from wpedit.c and command.c	F6BVP		*
 *                                                    	*
 ********************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <ctype.h>
#include <time.h>
#include <sys/file.h>
#include <sys/ioctl.h>

#include "ax25compat.h"
#include "wp.h"

int main(int argc, char **argv)
{
	int nb = 1000;
	unsigned int flags = 0;
	int p;
	int i, j;
	wp_t *wp;
	char *add;
	char *call;
	char dnic[5];
	char buf[20];

	if (argc < 2)
	   {
	   printf ("Usage: wplist [-acdnrl number] <callsign index>\n");
	   printf ("options :  -n = nodes only  -l = max number of answers\n");       
	   printf ("sort by :  -a address  -c callsign (default)  -d date  -r reverse\n");
	   return (1);
	   }

	optind = 0;

	while ((p = getopt(argc, argv, "acdl:nr")) != -1)
	{
		switch (p)
		{
		case 'c':
			flags &= ~(WP_ADDRSORT_FLAG | WP_DATESORT_FLAG);
			break;
		case 'l':
			nb = strtoul(optarg, NULL, 10);
			break;
		case 'n':
			flags |= WP_NODE_FLAG;
			break;
		case 'r':
			flags |= WP_REVERSE_FLAG;
			break;
		case 'a':
			flags &= ~(WP_DATESORT_FLAG);
			flags |= WP_ADDRSORT_FLAG;
			break;
		case 'd':
			flags &= ~(WP_ADDRSORT_FLAG);
			flags |= WP_DATESORT_FLAG;
			break;
		}
	}

	if (optind == argc)
		argv[optind] = "*";
	else
		strcat(argv[argc-1], "*");

	if (wp_open("NODE") == 0) {

	printf("Callsign  Last update UTC   DNIC address N/U   status\n");

	if (wp_get_list(&wp, &nb, flags, argv[optind]) != -1)
	{
		for (i = 0; i < nb; i++)
		{
			if (wp[i].date == 0L)
				break;

			add = rose_ntoa(&wp[i].address.srose_addr);
			call = ax25_ntoa(&wp[i].address.srose_call);

			strncpy(dnic, add, 4);
			dnic[4] = '\0';

			my_date(buf, wp[i].date);
			printf("%-9s %s => %s %-7s ", call, buf, dnic, add + 4);

			if (wp[i].is_node)
				printf("Node ");
			else
				printf("     ");

			if (wp[i].is_deleted)
				printf(" DELETED ");
			else
				printf(" ------- ");

			for (j = wp[i].address.srose_ndigis - 1; j >= 0; j--)
			{
				printf("%s ", ax25_ntoa(&wp[i].address.srose_digis[j]));
			}

			printf("%s %s\n", wp[i].locator, wp[i].city);
		}
	}

	if (nb == 0)
	{
		printf("No WP matching \"%s\" !\n", argv[optind]);
		return (1);
	}

	printf("\nFPAC White Pages database : %d callsigns\n", wp_nb_records());

	wp_free_list(&wp);
	wp_close();
	}
	else {
		printf("Cannot open WP \n");
		return (1);
	}
	return (0);
}

