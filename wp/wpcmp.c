
/******************************************************
 * wp/wpcmp.c                                         *
 * FPAC project.            wpcmp                     *
 *                                                    *
 * Compares two database files                        *
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
#include "fpac.h"
#include "wp.h"
#include "crc.h"

void diff(FILE *ftpr1, FILE *ftpr2);
int insert(int db, unsigned record, wp_t *wp);

void usage(void)
{
	printf("wpcmp compare two WP databases\n\tusage: wpcmp db_file_1 db_file_2\n");
}

int main(int ac, char **av)
{
	FILE *fptr1;
	FILE *fptr2;
	char *file1;
	char *file2;
	wp_t wp;
	wp_header wph;
	int verbose = 0;
	int p;
	unsigned record;

	if (ac < 3) {
		usage();
		return(1);
	}

	while ((p = getopt(ac, av, "v")) != -1) 
	{
		switch (p)
		{
		case 'v' :
			verbose = 1;
			break;
		case '?' :
			usage();
			return(1);
		}
	}

	file1 = av[optind];
	file2 = av[optind+1];

	fptr1 = fopen(file1, "r");
	if (fptr1 == NULL)
	{
		printf("db_file %s not found\n", file1);
		return(0);
	}

	fptr2 = fopen(file2, "r");
	if (fptr1 == NULL)
	{
		printf("db_file %s not found\n", file2);
		return(0);
	}

	if (fread(&wph, sizeof(wph), 1, fptr1) == 0)
	{
		fclose(fptr1);
		return(0);
	}

	/* Check the first record for compatibility */
	if (strcmp(wph.signature,FILE_SIGNATURE) != 0)
	{
		printf("WP file %s is not compatible\n", file1);
		fclose(fptr1);
		return(0);
	}

	printf("%d callsigns in database 1\n", wph.nb_record);

	if (fread(&wph, sizeof(wph), 1, fptr2) == 0)
	{
		fclose(fptr1);
		fclose(fptr2);
		return(0);
	}

	/* Check the first record for compatibility */
	if (strcmp(wph.signature,FILE_SIGNATURE) != 0)
	{
		printf("WP file %s is not compatible\n", file2);
		fclose(fptr1);
		fclose(fptr2);
		return(0);
	}

	printf("%d callsigns in database 2\n", wph.nb_record);

	printf("Reading database 1 ...\n");
	
	record = 0;
	while (fread(&wp, sizeof(wp_t), 1, fptr1))
	{
		insert(1, record, &wp);
		++record;			
	}
	
	printf("Reading database 2 ...\n");

	record = 0;
	while (fread(&wp, sizeof(wp_t), 1, fptr2))
	{
		insert(2, record, &wp);
		++record;			
	}

	diff(fptr1, fptr2);
		
	fclose(fptr1);
	fclose(fptr2);
	
	return 0;
}

typedef struct db_info
{
	struct db_info *next;
	char call[10];
	unsigned rec1, rec2;
	unsigned short crc1, crc2;
	time_t date1, date2;
} dbinfo;

dbinfo *db = NULL;

dbinfo *search(char *call)
{
	dbinfo *p = db;
	
	while (p)
	{
		if (strcmp(call, p->call) == 0)
			return p;
		p= p->next;
	}
	
	return NULL;
}

int insert(int dbnum, unsigned record, wp_t *wp)
{
	unsigned short crc;
	dbinfo *d;
	unsigned char crc_buf[4];
	char *call;
	
	call = ax25_ntoa(&wp->address.srose_call);
	if (*call == '\0')
		return 0;
		
	d = search(call);
	if (d == NULL)
	{
		d = calloc(sizeof(dbinfo), 1);
		d->next = db;
		db = d;
	}

	crc16_cumul(NULL, 0);
	*(unsigned short *)crc_buf = 0;
	crc = crc16_cumul(crc_buf, 2);
	*(unsigned int *)crc_buf = wp->date;
	crc = crc16_cumul(crc_buf, 4);			
	crc = crc16_cumul((unsigned char *)&wp->address, sizeof(struct full_sockaddr_rose));

	strcpy(d->call, call);
	switch (dbnum)
	{
	case 1:
		d->rec1 = record;
		d->crc1 = crc;
		d->date1 = wp->date;
		break;
	case 2:
		d->rec2 = record;
		d->crc2 = crc;
		d->date2 = wp->date;
		break;
	}
	
	return 1;
}

char *mtime(char *buf, time_t date)
{
	struct tm *tm;
	
	tm = localtime(&date);
	
	if (date == 0L)
	{
		sprintf(buf, "                 ");
	}
	else
	{
		sprintf(buf, "%02d:%02d:%02d %02d/%02d/%02d",
			tm->tm_hour, tm->tm_min, tm->tm_sec,
			tm->tm_mday, tm->tm_mon+1, tm->tm_year%100);
	}
	return buf;
}

void get_record(FILE *fptr, unsigned rec, wp_t *wp)
{
	fseek(fptr, sizeof(wp_header) + rec * sizeof(wp_t), SEEK_SET);
	fread(wp, sizeof(wp_t), 1, fptr);
}

void diff(FILE *fptr1, FILE *fptr2)
{
	dbinfo *p = db;
	char buf1[40];
	char buf2[40];
	wp_t wp;
	
	while (p)
	{
		if (p->crc1 != p->crc2)
		{
			printf("\n-->%s : (1) %s  (2) %s\n", 
				p->call, 
				mtime(buf1, p->date1),
				mtime(buf2, p->date2));
			get_record(fptr1, p->rec1, &wp);
			dump_rose("database (1)", &wp.address);
			get_record(fptr2, p->rec2, &wp);
			dump_rose("database (2)", &wp.address);
		}
		p = p->next;
	}
}
