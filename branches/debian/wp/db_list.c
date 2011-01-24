
/******************************************************
 * wp/dblist.c                                        *
 * FPAC project.            FPAC PAD                  *
 *                                                    *
 * Parts of code from different sources of ax25-utils *
 *                                                    *
 * F6FBB 05-1997                                      *
 *                                                    *
 ******************************************************/

/******************************************************
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

#include "ax25compat.h"
#include "wp.h"

static int node;
static int addsort;
static int revsort;
static int datesort;

extern wp_header *db_header;		/* Database file header */
extern wp_t *db_records;		/* Database file records */

/*** Prototypes *******************/
static int wp_cmp(const void *p1, const void *p2);
static int date_comp(const void *a, const void *b);

#define CR() printf( (cr) ? "\r" : "\n"); 

/* Cache for node list to be implemented */

void db_list_free(wp_t **wp)
{
	if (*wp)
		free(*wp);
	*wp = NULL;
}

int db_list_get(list_req_t *req, wp_t **pwp, int *nb)
{
	int ok;
	int pos;
	int ssid = -1;
	int limit;
	int index;
	int test_call;
	wp_t *wp;
	char call[20];
	char *full_call;
	char *ptr, *add;
	char match[10];
	unsigned int *sorted_list = NULL;
	int ofst;

	node = 0;
	addsort = 0;
	revsort = 0;
	datesort = 0;

	*nb = 0;
	*pwp = NULL;
	
	limit = req->max;
	
	node = (req->flags & WP_NODE_FLAG);
	revsort = (req->flags & WP_REVERSE_FLAG);
	addsort = (req->flags & WP_ADDRSORT_FLAG);
	datesort = (req->flags & WP_DATESORT_FLAG);

	strcpy(match, req->mask);

	test_call = 0;

	for (ptr = match ; *ptr ; ptr++)
	{
		if (isalpha(*ptr))
			test_call = 1;
	}
	
	if ((ptr = strrchr(match, '-')) != NULL)
	{
		ssid = atoi(ptr+1);
	}

	wp = malloc(sizeof(wp_t) * (limit + 1));
	if (wp == NULL)
		return -1;

	pos = 0;
	
	/* Sort by dates if needed */
	if (datesort)
	{
		sorted_list = calloc(db_header->nb_record, sizeof(*sorted_list));
		if (!sorted_list) return -1;
	
		for (index=0; index<db_header->nb_record; index++)
			sorted_list[index] = index;
	
		/* Sort in date descending order */
		qsort(sorted_list, db_header->nb_record, sizeof(*sorted_list), date_comp);
	}
	
	for (index = 0 ; index < db_header->nb_record ; index++)
	{
/*		char *add;
		int ofst;
*/		
		if (sorted_list)
			ofst = sorted_list[index];
		else
			ofst = index;
		
		if (db_records[ofst].is_deleted)
			continue;
			
		full_call = ax25_ntoa(&db_records[ofst].address.srose_call);
		if (*full_call == '\0')
			continue;
			
		if (node && !db_records[ofst].is_node)
			continue;
			
		if (test_call)
		{
			strcpy(call, full_call);
			if (ssid == -1  && (ptr = strrchr(call, '-')) != NULL)
				*ptr = '\0';

			if (strmatch(call, match))
			{
				wp[pos] = db_records[ofst];
				if (pos < limit)
					++pos;
				else
					break;
			}
		}
		else
		{
			add = rose_ntoa(&db_records[ofst].address.srose_addr);
			ok = (strmatch(add, match));
			if (!ok)
				ok = (strmatch(add+4, match));
			if (ok)
			{
				wp[pos] = db_records[ofst];
				if (pos < limit)
					++pos;
				else
					break;
			}
		}
	}

	if (sorted_list)
	{
		free(sorted_list);
	}
	else if (pos)
	{
		/* sort the results if not by date */
		qsort(wp, pos, sizeof(wp_t), wp_cmp);
	}

	*nb = pos;
	*pwp = wp;
	
	return(0);
}

static int date_comp(const void *a, const void *b)
{
	unsigned int ia, ib;
	
	if (revsort)
	{
		ia = *(unsigned int *)b;
		ib = *(unsigned int *)a;
	}
	else
	{
		ia = *(unsigned int *)a;
		ib = *(unsigned int *)b;
	}
	
	if (db_records[ia].date < db_records[ib].date) return 1;
	if (db_records[ia].date > db_records[ib].date) return -1;
	return 0;
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
	
	if (datesort)
	{
		if (wp1->date > wp2->date)
			ret = 1;
		else if (wp1->date < wp2->date)
			ret = -1;
		else
			ret = 0;
	}
 	else if (addsort)
	{
		pt1 = strdup(rose_ntoa(&wp1->address.srose_addr));
		pt2 = strdup(rose_ntoa(&wp2->address.srose_addr));
		ret = strcmp(pt1, pt2);
		free(pt1);
		free(pt2);
	}
	else
	{
		pt1 = strdup(ax25_ntoa(&wp1->address.srose_call));
		pt2 = strdup(ax25_ntoa(&wp2->address.srose_call));
		ret = strcmp(pt1, pt2);
		free(pt1);
		free(pt2);
	}
	
	return ret;
}

