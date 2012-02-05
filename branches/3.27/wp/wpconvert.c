
/******************************************************
 * wp/wpconvert.c                                     *
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

typedef struct {
	time_t date;
	struct sockaddr_rose address;
	char name[22];
	char city[22];
	char qra_locator[7];
	char free[24];		/* For futur extension */
} old_wp_t;

#define OLD_FILE_SIGNATURE	"FPACWP_V001"

int main(int ac, char **av)
{
	FILE *fptr;
	old_wp_t old_wp;
	wp_t new_wp;
	wp_header wph;
	char dnic[5];
	char *add, *call;
	char buf[20];

	if (ac < 2)
	{
		fprintf(stderr, "format : wpconv database\n");
		return 1;
	}
	
	fptr = fopen(av[1], "r");
	if (fptr == NULL)
	{
		fprintf(stderr, "Cannot open %s\n", av[1]);
		return 2;
	}

	if (fread(&wph, sizeof(wph), 1, fptr) == 0)
	{
		fclose(fptr);
		fprintf(stderr, "database %s is empty\n", av[1]);
		return 3;
	}

	/* Check the first record for compatibility */
	if (strcmp(wph.signature,OLD_FILE_SIGNATURE) != 0)
	{
		fprintf(stderr, "database %s is not compatible\n", av[1]);
		fclose(fptr);
		return 4;
	}

	printf("%u callsigns in database\n", wph.nb_record);

	if (wp_open("WPCNVT")) {
		perror("Cannot open WP service");
		exit(1);
	}
	
	while (fread(&old_wp, sizeof(old_wp_t), 1, fptr))
	{
			
		if (old_wp.date == 0)
			continue;
			
		add = rose_ntoa(&old_wp.address.srose_addr);
		call = ax25_ntoa(&old_wp.address.srose_call);

		if (wp_check_call(call) != 0)
			continue;

		strncpy(dnic, add, 4); dnic[4] = '\0';

		my_date(buf, old_wp.date),
		
		printf("%-9s %s => %s %-7s ", 
				call,
				buf,
				dnic, 
				add+4);
			
		if (old_wp.address.srose_ndigis)
			printf("%s ", ax25_ntoa(&old_wp.address.srose_digi));
						
		printf("%s %s\n", old_wp.name, old_wp.city);
		
		/* Add to the running database */
		
		memset(&new_wp, 0, sizeof(wp_t));
		
		new_wp.date = old_wp.date;
		new_wp.address.srose_addr     = old_wp.address.srose_addr;
		new_wp.address.srose_call     = old_wp.address.srose_call;
		new_wp.address.srose_digis[0] = old_wp.address.srose_digi;
		new_wp.address.srose_ndigis   = old_wp.address.srose_ndigis;

		wp_set(&new_wp);
	}

	wp_close();

	return(0);
}

