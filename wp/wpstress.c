
/******************************************************
 * wp/wpstress.c                                      *
 * FPAC project.            FPAC WP EDITION           *
 *                                                    *
 * Parts of code from different sources of ax25-utils *
 *                                                    *
 * F6FBB 05-1997                                      *
 *                                                    *
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

static void dump_record(wp_t *wp)
{
	int i;
	char *add;
	char dnic[5];
	char buf[20];

	add = rose_ntoa(&wp->address.srose_addr);
	strncpy(dnic, add, 4); dnic[4] = '\0';

	my_date(buf, wp->date);

	printf("%-9s %s => %s %s ", 
	       ax25_ntoa(&wp->address.srose_call), 
	       buf, dnic, add+4);

	for (i = 0 ; i < wp->address.srose_ndigis ; i++)
	{
		printf("%s ", ax25_ntoa(&wp->address.srose_digis[i]));
	}
	
	printf("%s %s", wp->name, wp->city);
}

/****************************************************************************/

static void Usage(void)
{
	fprintf(stderr, "Usage : wpstress nb_records\n");
	exit(1);
}

int main(int argc, char *argv[])
{
	char call[13];
	ax25_address addr;
	wp_t wp;
	int nb_records;
	int rc;
	unsigned char *record_exist;
	int index;
	int nb_set = 0;
	int nb_get = 0;
	int nb_rec = 0;
	int error = 0;
	
	if (argc != 2) Usage();
	nb_records = atoi(argv[1]);
	
	record_exist = calloc(sizeof(*record_exist), nb_records);
	
	if (wp_open("WPSTRS")) {
		perror("Cannot open WP service");
		exit(1);
	}
	
	while (!error) {
		/* Generate a random call */
		index = rand() % nb_records;
		sprintf(call, "F%05d", index);
		ax25_aton_entry(call, addr.ax25_call);
		
		if (rand() & 1) { /* Set a record */
			nb_get++;
			printf("Get ");
			rc = wp_get(&addr, &wp);
			if ((rc?1:0) ^ record_exist[index]) {
				dump_record(&wp);
				printf(" OK");
			}
			else {
				printf("%-9s ERROR", ax25_ntoa(&addr));
				error = 1;
			}
		}
		else {            /* Get a record */
			nb_set++;
			printf("Set ");
			memset(&wp, 0, sizeof(wp_t));
			wp.address.srose_call = addr;
			dump_record(&wp);
			rc = wp_set(&wp);
			if (rc) {
				printf(" ERROR");
				error = 1;
			}
			else {
				printf(" OK");
				if (!record_exist[index]) {
					record_exist[index] = 1;
					nb_rec++;
				}
			}
		}
		printf(" NbSet=%d NbGet=%d NbRec=%d\n", nb_set, nb_get, nb_rec);
	}
	
	exit(1);
}
