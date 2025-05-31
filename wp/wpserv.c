
/******************************************************
 * wp/wpserv.c                                        *
 * FPAC project.            FPAC PAD                  *
 *                                                    *
 * Parts of code from different sources of ax25-utils *
 *                                                    *
 * F6FBB 05-1997                                      *
 *                                                    *
 ******************************************************/

/******************************************************
 * wpserv.c
 *
 * 10/nov/2006 1.1 cosmetic display modifications
 * 12/05/97 1.00 F6FBB First draft !
 *
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

#include "../pathnames.h"
#include "ax25compat.h"
#include "wp.h"

int cr = 0;
int addsort = 0;
int revsort = 0;
int node = 0;
int valid = 0;	 /* number of valid i.e. not "deleted" records */
int deleted = 0; /* number of deleted records */

/*** Prototypes *******************/
static void display_results(wp_t *wp, int limit, int nb);
static int wp_cmp(const void *p1, const void *p2);

#define CR() printf( (cr) ? "\r" : "\n"); 

void now_date(char *buf);

int usage(void)
 {
	printf("\n");
	printf("Wpserv (version %s)\n",__DATE__);
	printf("usage: wpserv [-l<nr> display nr records] [-n<nr> display nr nodes] [mask<*>]\n");
	printf("              [-a sort by address] [-r reverse sort] [-c remove new line from output]\n");
	printf("\n");
	exit (0);
}

int main(int ac, char **av)
{
	int p;
	int ok;
	int opt = 0;
	int pos;
	int nb;
	int nbr = 0;
	int limit = 30;
	int test_call;
	FILE *fptr;
	wp_t *wp;
	wp_header wph;
	char call[20];
	char buf[30];
	char *full_call;
	char *ptr;
	char *match;
	char *add;

	while ((p = getopt(ac, av, "acrn:l:?" )) != -1)
	{
		switch (p)
		{
		case 'c' :
			cr = 1;
			opt += 1;
			break;
		case 'l' :
			nbr = strtoul(optarg, NULL, 10);
			opt += 1;
			break;
		case 'a' :
			addsort = 1;
			opt += 1;
			break;
		case 'r' :
			revsort = 1;
			opt += 1;
			break;
		case 'n' :
			node = 1;
			nbr = strtoul(optarg, NULL, 10);
			opt += 1;
			break;
		case '?' :
/*	   		printf("Wpserv (version %s)\n",__DATE__);
			printf("usage: wpserv [-l <n> display n records] [-n <n> display n nodes] [mask<*>]\n");
			printf("              [-a sort by address] [-r reverse sort] [-c remove new line from output]\n");
			return(1);
*/
			usage();
			break;
		}
	}

/* Print the Current Date/time */
	now_date(buf);
	printf ("\tWPserv - %s",buf);

	match = strdup("*");
//	test_call = 1;

	if (ac == 1) usage();

	if (ac == 2 && (opt == 1)) {
		match = "*\0";
	}
	else {
		match = av[ac-1];
		strcat(match,"*\0");
	}
	
	test_call = 0;
	
	for (ptr = match ; *ptr ; ptr++) {
		if (isalnum(*ptr)) { 
			*ptr = toupper((int)*ptr);
			test_call = 1;
		}
	}

	if (limit == 0) limit = 10;
//	printf("ac %d opt %d mask : %s\n",ac, opt, match);

	fptr = fopen(FPACWP, "r");
	if (fptr == NULL)
	{
		printf("WP data base %s not found",FPACWP); CR();
		return(0);
	}

	if (fread(&wph, sizeof(wph), 1, fptr) == 0)
	{
		fclose(fptr);
		return(0);
	}

	/* Check the first record for compatibility */
	if (strcmp(wph.signature,FILE_SIGNATURE) != 0)
	{
		printf("WP file version %s is not compatible\n",FILE_SIGNATURE);
		fclose(fptr);
		return(0);
	}

	printf("\t%d callsigns in WP database", wph.nb_record);
	CR();

	if ((ptr = strrchr(match, '-')) != NULL)
		*ptr = '\0';

	wp = malloc(sizeof(wp_t) * (limit + 1));
	if (wp == NULL)
	{
		printf("Cannot allocate enough memory"); CR();
		return(0);
	}

	pos = 0;
	nb = 0;
	if (nbr ==0) nbr = limit;

	while (fread(&wp[pos], sizeof(wp_t), 1, fptr))
	{
		full_call = ax25_ntoa(&wp[pos].address.srose_call);
		if (*full_call == '\0')
			continue;
			
		if (node && !wp[pos].is_node)
			continue;

		if (wp[pos].is_deleted != 1)
			valid ++;
		else
			deleted++;
			
		if (test_call)
		{
			strcpy(call, full_call);
			if ((ptr = strrchr(call, '-')) != NULL)
				*ptr = '\0';

			if (strmatch(call, match))
			{
				++nb;			
				if (pos < limit)
					++pos;
			}
		}
		else
		{
			add = rose_ntoa(&wp[pos].address.srose_addr);
			ok = (strmatch(add, match));
			if (!ok)
				ok = (strmatch(add+4, match));
			if (ok)
			{
				++nb;			
				if (pos < limit)
					++pos;
			}
		}
		
	}

	fclose(fptr);
	if (nbr >= nb) nbr = nb;
	if (nbr)
	{
		/* sort the results */
		qsort(wp, (nbr < limit) ? nbr : limit, sizeof(wp_t), wp_cmp);
	}
	
	display_results(wp, limit, nbr);
	
	free(wp);
	return(0);
}

static int wp_cmp(const void *p1, const void *p2)
{
	int ret;
	char *pt1;
	char *pt2;
	wp_t *wp1;
	wp_t *wp2;
	
	if (revsort)
	{
		wp1 = (wp_t *)p2;
		wp2 = (wp_t *)p1;
	}
	else
	{
		wp1 = (wp_t *)p1;
		wp2 = (wp_t *)p2;
	}
	
	if (addsort)
	{
		pt1 = strdup(rose_ntoa(&wp1->address.srose_addr));
		pt2 = strdup(rose_ntoa(&wp2->address.srose_addr));
	}
	else
	{
		pt1 = strdup(ax25_ntoa(&wp1->address.srose_call));
		pt2 = strdup(ax25_ntoa(&wp2->address.srose_call));
	}
	
	ret = strcmp(pt1, pt2);
	
	free(pt1);
	free(pt2);
	
	return ret;
}

static void display_results(wp_t *wp, int limit, int nb)
{
	int i, j;
	char dnic[5];
	char *add;
	char *call;
	char buf[20];	/* my_date string */

	if (nb == 0)
	{
		CR();
		printf("None found");
		CR();
	}

	else
	{
		if (nb > limit)
		{
			printf("%d entries match your request. Only first %d will be displayed.", nb, limit);
			CR();
			CR();
			nb = limit;
		}

		printf("Callsign  Last update UTC   DNIC address N/U  \tDigi \tLocator City \tStatus\n");
		CR();

		for (i = 0 ; i < nb ; i++)
		{
			my_date(buf, wp->date);

			add = rose_ntoa(&wp->address.srose_addr);
			call = ax25_ntoa(&wp->address.srose_call);

			if (strstr(call,"-") == NULL)
				strcat(call,"-0");

			strncpy(dnic, add, 4);
			dnic[4] = '\0';

			printf("%-9s %s => %s %-7s", call, buf, dnic, add + 4);

			if (wp->is_node)
				printf(" Node ");
			else
				printf(" User ");

			if (wp->address.srose_ndigis == 0)
				printf("\t - ");

			for (j = wp->address.srose_ndigis - 1; j >= 0; j--)
			{
				call = ax25_ntoa(&wp->address.srose_digis[j]);
				if (strstr(call,"-") == NULL)
					strcat(call,"-0");
				printf("\t%-9s ", call);				
			}

			printf("\t%s \t%s", wp->locator, wp->city);
			
			if (wp->is_deleted == 1)
				printf("\tDELETED\n");
			else
				printf("\tOk\n");

			++wp;
		}
	CR();
	}
	printf("\n%d deleted records found\n",deleted);
	printf("%d valid records found\n",valid); 
	printf("--\n");

}

