
/******************************************************
 * wp/wpmaint.c                                       *
 * FPAC project.            FPAC PAD                  *
 *                                                    *
 * Parts of code from different sources of ax25-utils *
 *                                                    *
 * F6FBB 05-1997                                      *
 *                                                    *
 ******************************************************/

/******************************************************
 * Nov/15/06 2.01 deletes and actually erases old records
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

/*** Prototypes *******************/

#define CR() printf( (cr) ? "\r" : "\n"); 
#define ERASETIME 7L 
#define DELETETIME 180L 

int main(int argc, char **argv)
{
	int i, p;
	int ok = 0;
	int nb = 0;
	int retour = 0;
	FILE *fptr_i;
	FILE *fptr_o;
	wp_t wp;
	wp_header wph;
	wp_header wph_sig;
	char *full_call;
	char fpacwp_old[1024];
	char *add;
	char dnic[5];
	time_t temps;
	time_t delete_temps;
	time_t erase_temps;
	char buf[20];
	char up_date[20];
	int e_temps = ERASETIME;
	int d_temps = DELETETIME;

	if (argc < 2)
	{
		printf ("Wpmaint (%s)\n", __DATE__);
		printf ("Usage: wpmaint [-argument]\n");
		printf ("argument :  -d = age delay (in days) for deleting old records\n");       
		printf ("            -e = age delay (in days) for erasing deleted records\n");       
		printf ("defaults delays : %d days before deletion and %d days before erasing deleted records\n",d_temps, e_temps);       
	}

	optind = 0;

	while ((p = getopt(argc, argv, "d:e:")) != -1)
	{
		switch (p)
		{
		case 'e':
			e_temps = strtoul(optarg, NULL, 10);
			break;
		case 'd':
			d_temps = strtoul(optarg, NULL, 10);
			break;
		}
	}

	if (optind == argc)
		argv[optind] = "*";
	else
		strcat(argv[argc-1], "*");

	temps = time(NULL);
	delete_temps  = temps - (3600L * 24L * (long)d_temps);
	erase_temps   = temps - (3600L * 24L * (long)e_temps);
	
	strcpy(fpacwp_old, FPACWP);
	strcat(fpacwp_old, ".old");

	/* Checks FPAC WP file */	
	fptr_i = fopen(FPACWP, "r");

	if ((fptr_i != NULL) && (fread(&wph_sig, sizeof(wph), 1, fptr_i) != 0));
	else
	{
		fprintf(stderr, "Could not find %s or file is corrupted ... Trying %s !\n", FPACWP, fpacwp_old);

	/* Do we have a backup FPACWP */	
		fptr_i = fopen(fpacwp_old, "r");
		if (fptr_i == NULL)
		{
			fprintf(stderr, "Could not find %s Exiting !\n", fpacwp_old);
			return(1);
		}

	/* Lets use it */	
		if (rename(fpacwp_old, FPACWP) != 0)
		{
			fprintf(stderr, "Could not rename %s to %s ... Exiting !\n", fpacwp_old, FPACWP);
			return(2);
		}
	}

	fclose(fptr_i);

	/* Save FPACWP file */
	if (rename(FPACWP, fpacwp_old) != 0)
		{
			fprintf(stderr, "Could not rename %s to %s ... Exiting !\n", FPACWP, fpacwp_old);
			return(4);
		}
	/* Read saved FPACWP file */
	fptr_i = fopen(fpacwp_old, "r");
	if (fptr_i == NULL)
		{
			fprintf(stderr, "Could not open %s Exiting !\n", fpacwp_old);
			return(5);
		}
	/* Read the first record */
	if (fread(&wph_sig, sizeof(wph), 1, fptr_i) == 0)
		{
			fprintf(stderr, "No signature found ... %s\n", fpacwp_old);
			fclose(fptr_i);
			return(6);
		}

	/* Check signature for version compatibility */
	if (strcmp(wph_sig.signature,FILE_SIGNATURE) != 0)
		{
			fprintf(stderr, "WP file is not compatible\n");
			fclose(fptr_i);
			return(7);
		}
	/* Lets create new FPACWP */
	fptr_o = fopen(FPACWP, "w");
	if (fptr_o == NULL)
	{
		fprintf(stderr, "Could not create %s ... Exiting !\n", FPACWP);
		return(8);
	}

	printf("%d records in old WP database\n", wph_sig.nb_record);
	printf("Records older than %d days will be marked 'deleted'\n",d_temps);
	printf("Records marked 'deleted' erased after %d days\n", e_temps);

	if (fwrite(&wph_sig, sizeof(wph), 1, fptr_o) == 0)
	{
		fprintf(stderr, "Cannot write signature in %s ... Exiting\n", FPACWP);
		retour = 3;
	}

	while (fread(&wp, sizeof(wp_t), 1, fptr_i))
	{
		add = rose_ntoa(&wp.address.srose_addr);

		full_call = ax25_ntoa(&wp.address.srose_call);
		if (*full_call == '\0')
			continue;

		if (wp_check_call(full_call) != 0)
		{
			printf("Illegal callsign %s : discarded\n", full_call);
			continue;
		}

		 strncpy(dnic, add, 4); dnic[4] = '\0';

		if (wp.is_node < 0 || wp.is_node > 1)
		{
			printf("Illegal is_node %d : discarded\n", wp.is_node);
			continue;
		}

		my_date(buf, wp.date);

/* Records marked deleted and older than "e_temps" delay are erased i.e. not copied */
		if (wp.is_deleted && wp.date > erase_temps) {
			printf("%-9s %s => %s %-7s", full_call, buf, dnic, add+4);
			printf("%s", " deleted record ERASED");
			printf("\n");
			continue;
		}
/* Records older than "d_temps" days are marked DELETED */ 	
		printf("%-9s %s => %s %-7s", full_call, buf, dnic, add+4);
		if (wp.is_node == 0)
			printf("%s"," user ");
		else
			printf("%s"," node ");
		if (wp.date < delete_temps) {
			wp.is_deleted = 1;
			printf("%s", " deleted ");
			wp.date = temps;
		}

/* Records dated after present time are set to present time */
		if (wp.date > temps) {
			my_date(up_date, temps);
			printf(" %-9s  date ERROR set to %s ", full_call, up_date);
			wp.date = temps;
		}
		
		ok = 1;
		for (i = 0 ; i < wp.address.srose_ndigis ; i++)
		{
			full_call = ax25_ntoa(&wp.address.srose_digis[i]);
			if (wp_check_call(full_call) != 0)
			{
				printf("Illegal digi %s : discarded\n", full_call);
				ok = 0;
				break;
			}
		}

		if (!ok)
		{
			printf("\n");
			continue;
		}

		printf("\n");

		if (fwrite(&wp, sizeof(wp_t), 1, fptr_o) == 0)
		{
			fprintf(stderr, "Cannot write wp record in %s ... Exiting\n", FPACWP);
			retour = 3;
		}

		++nb;

	}

	if (nb != wph_sig.nb_record)
	{
		wph_sig.nb_record = nb;
		rewind(fptr_o);
		
		if (fwrite(&wph_sig, sizeof(wph), 1, fptr_o) == 0)
		{
			fprintf(stderr, "Cannot write signature in %s ... Exiting\n", FPACWP);
			retour = 3;
		}


	}

	printf("%d records in new WP database\n", wph_sig.nb_record);

	fclose(fptr_i);
	fclose(fptr_o);
	return(retour);
}

