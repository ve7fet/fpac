/*
 * wp/db.c
 *
 * FPAC project
 *
 * WP database handler : low level mapped file I/O
 *
 * F1OAT 9080117
 */

#include "wpdefs.h"
#include "update.h"
#include "db.h"
#include "crc.h"
#include "hash.h"

/* Database memory mapping vars */

static int map_size;			/* Size of current memory mapping */
static void *db_map_addr = NULL;	/* Pointer to database mmap() file */
static int db_size;				/* Number of records in database */
static int hash_table;			/* Handle returned by hash_open */
static char wp_file[256];		/* Default file */

wp_header *db_header;			/* Database file header */
wp_t *db_records;				/* Database file records */

static int ax25_check_call(ax25_address * a)
{
	/*int n;
	   char c;

	   for (n = 0; n < 6; n++) {
	   c = (a->ax25_call[n] >> 1) & 0x7F;
	   if ((c != ' ') && (c < 'A' || c > 'Z') && (c < '0' || c > '9'))
	   return 0;
	   }
	   return 1; */
	return (wp_check_call(ax25_ntoa(a)) == 0);
}

static int db_valid(wp_t * wp)
{
	int n;
	time_t temps, time_before, time_after;
	char buf[20];
	char buf1[20];
	char buf2[20];

	/* Check the Callsign */
	if (!ax25_check_call(&wp->address.srose_call))
	{
		syslog(LOG_INFO, "Invalid record : callsign error");
		return 0;
	}

	/* Check the Digi and number of digis */
	if (wp->address.srose_ndigis > ROSE_MAX_DIGIS)
	{
		syslog(LOG_INFO, "Invalid record : digi number");
		return 0;
	}
	for (n = 0; n < wp->address.srose_ndigis; n++)
	{
		if (!ax25_check_call(&wp->address.srose_digis[n]))
		{
			syslog(LOG_INFO, "Invalid record : digi callsign error");
			return 0;
		}
	}

	/* Check the is_node info */
	if (wp->is_node < 0 || wp->is_node > 1)
	{
		syslog(LOG_INFO, "Invalid record : node error");
		return 0;
	}

	/* Check the is_node info */
	if (wp->is_deleted < 0 || wp->is_deleted > 1)
	{
		syslog(LOG_INFO, "Invalid record : deleted error");
		return 0;
	}

	/* Check the Date */
	/* should be between (current - 1) year and current + 1 week */
	temps = time(NULL);
	my_date( buf, wp->date);
	time_before = temps - (86400L * 365L);
	time_after = temps + 86400L;
	my_date( buf1, time_before);
	my_date( buf2, time_after);
	if ((wp->date < time_before ) || (wp->date > (time_after)))
	{
		syslog(LOG_INFO, "Invalid record : date %s outside of accepted window [%s-%s]", buf, buf1, buf2);
		return 0;
	}

	/* Check the Name */
	for (n = 0; n < 22; n++)
	{
		if (wp->name[n] == '\0')
		{
			break;
		}
	}
	if (n == 22)
	{
		syslog(LOG_INFO, "Invalid record : name error");
		return 0;
	}

	/* Check the City */
	for (n = 0; n < 22; n++)
	{
		if (wp->city[n] == '\0')
		{
			break;
		}
	}
	if (n == 22)
	{
		syslog(LOG_INFO, "Invalid record : city error");
		return 0;
	}

	/* Check the Locator */
	for (n = 0; n < 7; n++)
	{
		if (wp->locator[n] == '\0')
		{
			break;
		}
	}
	if (n == 7)
	{
		syslog(LOG_INFO, "Invalid record : locator error");
		return 0;
	}

	return 1;
}

void db_info(struct wp_info *wpi)
{
	wpi->size = db_size;
	wpi->nbrec = db_header->nb_record;
}

static void db_create(wp_t * wpnode)
{
	char *stmp;
	char *ptr;
	FILE *fptr;
	wp_header wph;
	wp_t wp;
	int i;

	if (verbose)
		syslog(LOG_INFO, "Reset database");

	/* Create the directories */
	stmp = strdup(wp_file);

	ptr = strchr(stmp + 1, '/');
	while (ptr)
	{
		*ptr = '\0';
		mkdir(stmp, 0777);
		*ptr++ = '/';
		ptr = strchr(ptr, '/');
	}

	free(stmp);

	fptr = fopen(wp_file, "w");
	if (fptr == NULL)
		return;

	strcpy(wph.signature, FILE_SIGNATURE);
	wph.nb_record = 1;			/* Includes our node information */

	if (fwrite(&wph, sizeof(wph), 1, fptr) == 0)
	{
		fclose(fptr);
		unlink(wp_file);
		return;
	}

	/* Add our node information as 1st record */
	fwrite(wpnode, sizeof(wp_t), 1, fptr);

	/* Allocate some more empty records */

	memset(&wp, 0, sizeof(wp));
	for (i = 1; i < NEW_RECORD_CHUNK; i++)
	{
		fwrite(&wp, sizeof(wp), 1, fptr);
	}

	fclose(fptr);
}

static int db_map(void)
{
	int fd, rc;
	struct stat st;
	wp_header wph;

	/* Get database size */

	if (stat(wp_file, &st))
	{
		perror(wp_file);
		return -1;
	}

	/* Open database and map to memory */

	fd = open(wp_file, O_RDWR);
	if (fd < 0)
	{
		syslog(LOG_INFO, "WP file %s : Cannot open", wp_file);
		perror(wp_file);
		return -1;
	}

	/* Check header */

	rc = read(fd, &wph, sizeof(wph));
	if (rc != sizeof(wph))
	{
		syslog(LOG_INFO, "WP file %s : File's header too short", wp_file);
		close(fd);
		return -2;
	}

	if (strcmp(wph.signature, FILE_SIGNATURE) != 0)
	{
		syslog(LOG_INFO, "WP file %s : Wrong signature", wp_file);
		close(fd);
		return -2;
	}

	/* Convert size to number of records (free or not) */

	db_size = (st.st_size - sizeof(wp_header)) / sizeof(wp_t);
	map_size = sizeof(wp_header) + db_size * sizeof(wp_t);

	/* Check size */
	if (map_size != st.st_size)
	{
		syslog(LOG_INFO, "WP file %s : size error", wp_file);
		close(fd);
		return -2;
	}

	if (wph.nb_record > db_size)
	{
		syslog(LOG_INFO, "WP file %s : Nb records in db header too large", wp_file);
		close(fd);
		return -2;
	}

	db_map_addr =
		mmap(0, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (db_map_addr == (void *) -1)
	{
		db_map_addr = NULL;
		perror("mmap wp file");
		close(fd);
		return -1;
	}

	db_header = (wp_header *) db_map_addr;
	db_records = (wp_t *) (db_header + 1);
	close(fd);

	if (verbose)
		syslog(LOG_INFO, "WP file %s : Database size %d / %d used", wp_file, db_size,
			   db_header->nb_record);

	return 0;
}

static void db_unmap(void)
{
	if (db_map_addr)
	{
		munmap(db_map_addr, map_size);
		db_map_addr = NULL;
		db_header = NULL;
		db_records = NULL;
	}
}

static int db_increase_size(void)
{
	int fd, i, rc;
	wp_t wp;

	db_unmap();
	memset(&wp, 0, sizeof(wp));

	fd = open(wp_file, O_RDWR);
	if (fd < 0)
	{
		perror(wp_file);
		return -1;
	}

	lseek(fd, 0, SEEK_END);
	for (i = 0; i < NEW_RECORD_CHUNK; i++)
	{
		rc = write(fd, &wp, sizeof(wp));
		if (rc != sizeof(wp))
		{
			close(fd);
			perror("Cannot increase db size");
			syslog(LOG_INFO, "Cannot increase db size");
			return -1;
		}
	}
	close(fd);

	return db_map();
}

static void db_check(void)
{
	int i;
	int bad_nb;

	/* Check database and delete bad records */
	for (;;)
	{
		bad_nb = -1;
		for (i = 0; i < db_header->nb_record; i++)
		{
			if (!db_valid(&db_records[i]))
			{
				/* Invalid record */
				fprintf(stderr,
						"Record %s (%d) invalid in database. Deleting...\n",
						ax25_ntoa(&db_records[i].address.srose_call), i);
				bad_nb = i;
				break;
			}
		}
		if (bad_nb == -1)
		{
			/* Database is checked ok */
			break;
		}
		/* Compact the rest of the file */
		for (i = bad_nb; i < db_header->nb_record - 1; i++)
		{
			db_records[i] = db_records[i + 1];
		}
		memset(&db_records[i], 0, sizeof(wp_t));
		--db_header->nb_record;
	}
}

#define MAX_TRIES 10

int db_open(char *file, wp_t * wpnode)
{
	int rc;
	int i;
	int nb;
	int bad_nb;
	wp_header wph;
	wp_header wph_sig;
	FILE *fptr_i;
	char fpacwp_old[256];

	strcpy(wp_file, file);

	/* Checks FPAC WP file */	
	fptr_i = fopen(wp_file, "r");

	if ((fptr_i != NULL) && (fread(&wph_sig, sizeof(wph), 1, fptr_i) != 0)) {
		fclose(fptr_i);
	}
	/* Checks backup database file */
	else {
		fprintf (stderr, "WP database %s corrupted or missing\n", wp_file);
		strcpy(fpacwp_old, wp_file);
		strcat(fpacwp_old, ".old");
		fptr_i = fopen(fpacwp_old, "r");
		if ((fptr_i != NULL) && (fread(&wph_sig, sizeof(wph), 1, fptr_i) != 0)) {
		fprintf (stderr, "Copying backup database %s\n", fpacwp_old);
			rename(fpacwp_old, wp_file);
			fclose(fptr_i);
		}
	}

	/* Create database file if necessary */

	if (access(wp_file, R_OK | W_OK) ) {
		fprintf (stderr, "Initializing new WP database %s\n", wp_file);
		db_create(wpnode);
	}

	/* Map database in memory */

	rc = db_map();
	if (rc == -2)
	{
		db_create(wpnode);
		rc = db_map();
	}
	if (rc)
		return rc;

	/* 10 tries allowed */
	for (nb = 0; nb < MAX_TRIES; nb++)
	{
		bad_nb = -1;

		/* Check database */
		db_check();
/* DEBUG F6BVP node record is systematicaly updated */
		/* Update 1st record if necessary */
/*		if (memcmp
			((char *) wpnode + sizeof(time_t),
			 (char *) &db_records[0] + sizeof(time_t),
			 sizeof(wp_t) - sizeof(time_t)) != 0)
*/		{
			fprintf(stderr, "Node information updated\n");
			db_records[0] = *wpnode;
		}

		/* Open hash table */

		hash_table = hash_open(HASH_BITS, sizeof(wp_t),
							   offsetof(wp_t, address.srose_call),
							   sizeof(ax25_address), (void **) &db_records);
		if (hash_table < 0)
		{
			syslog(LOG_INFO, "Cannot create hash table");
			db_unmap();
			return -1;
		}

		/* Index records in hash table */

		for (i = 0; i < db_header->nb_record; i++)
		{
			rc = hash_put(hash_table, i);
			if (rc < 0)
			{
				hash_close(hash_table);
				if (rc == -2)
				{
					bad_nb = i;
					syslog(LOG_INFO, "Index record #%d duplicated. Deleted !",
						   bad_nb);
					/* bad_nb is duplicated - delete it */
					for (i = bad_nb; i < db_header->nb_record - 1; i++)
					{
						db_records[i] = db_records[i + 1];
					}
					memset(&db_records[i], 0, sizeof(wp_t));
					--db_header->nb_record;
					break;
				}
				syslog(LOG_INFO, "Cannot index record #%d", i);
				db_unmap();
				return -1;
			}
		}

		if (bad_nb == -1)
		{
			/* Database has been processed ok */
			break;
		}
	}

	if (nb == MAX_TRIES)
		return -1;
	return 0;
}

void db_close(void)
{
	db_unmap();
}

static int db_find(ax25_address * call)
{
	int index;

	index = hash_search(hash_table, call);
	return index;
}

int db_get(ax25_address * call, wp_t * wp)
{
	int index;

	index = db_find(call);
	if (index < 0)
		return -1;

	*wp = db_records[index];
	if (!db_valid(wp))
	{
		/* Should never occur ! */
		syslog(LOG_INFO, "Bad record %s rc=%d in database",
			   ax25_ntoa(&wp->address.srose_call), index);
		db_check();
		return -2;
	}
	return 0;
}

int db_set(wp_t * wp, int force)
{
	int index;
	int rc;

	if (!db_valid(wp))
		return -2;

	index = db_find(&wp->address.srose_call);
	if (index < 0)
	{
		index = (db_header->nb_record)++;
		if (db_header->nb_record > db_size)
		{
			rc = db_increase_size();
			if (rc < 0)
			{
				return -1;
			}
		}
		db_records[index] = *wp;
		rc = hash_put(hash_table, index);
	}
	else
	{
		if (db_records[index].date > wp->date)
			return -1;			/* I have newer record */
		if (memcmp(&db_records[index], wp, sizeof(*wp)) == 0)
			return -1;
		if (db_records[index].is_node)
		{
			/* Special case of node information. Should not be updated by user */
			if (force)
			{
				/* Updated by a node */
				db_records[index] = *wp;
				return index;
			}
			return -1;
		}
		if (!force)
		{						/* If force==0 and only date different, do nothing */
			wp_t mywp;

			mywp = *wp;
			mywp.date = db_records[index].date;
			if (memcmp(&db_records[index], &mywp, sizeof(mywp)) == 0)
				return -1;
		}
		db_records[index] = *wp;
	}

	return index;
}

static int date_comp(const void *a, const void *b)
{
	unsigned int ia = *(unsigned int *) a, ib = *(unsigned int *) b;

	if (db_records[ia].date < db_records[ib].date)
		return 1;
	else if (db_records[ia].date > db_records[ib].date)
		return -1;
	else
	{
		/* Same date. Sort is made on callsign */
		char call1[10], call2[10];
		strcpy(call1, ax25_ntoa(&db_records[ia].address.srose_call));
		strcpy(call2, ax25_ntoa(&db_records[ib].address.srose_call));
		return (strcmp(call1, call2));
	}
	return 0;
}

/*
 * If vector->date_base == 0, compute best date_base and interval parameters
 *
 * If dirty >= 0, set dirty bits for client context[dirty] according to vector mismatch
 * Dirty flags are set only for the first vector element
 */

void db_compute_vector(int dirty, vector_t * vector)
{
	unsigned int *sorted_list;
	int i, vec_index;
	unsigned char crc_buf[4];
	unsigned int treshold;
	unsigned short crc;
	int dirty_first;
	unsigned short mycrc[WP_VECTOR_SIZE];
	int dirty_cnt[WP_VECTOR_SIZE];
	int record_cnt[WP_VECTOR_SIZE];
	char atime[40];
	int stop_dirty = 0;

	memset(dirty_cnt, 0, sizeof(dirty_cnt));
	memset(record_cnt, 0, sizeof(record_cnt));

	sorted_list = calloc(db_header->nb_record, sizeof(*sorted_list));
	if (!sorted_list)
		return;

	for (i = 0; i < db_header->nb_record; i++)
	{
		sorted_list[i] = i;
	}

	/* Sort in date descending order */

	qsort(sorted_list, db_header->nb_record, sizeof(*sorted_list), date_comp);

	/* Compute best parameters ? */

	if (vector->date_base == 0)
	{
		if (db_header->nb_record > 0)
		{
			vector->date_base = db_records[sorted_list[0]].date;
			vector->interval =
				1 + (vector->date_base -
					 db_records[sorted_list[db_header->nb_record - 1]].date) /
				(1 << WP_VECTOR_SIZE);
		}
		else
		{
			vector->date_base = 1;
			vector->interval = 0;
		}
	}

	strcpy(atime, ctime(&vector->date_base));
	atime[strlen(atime) - 1] = 0;

	if (verbose)
		syslog(LOG_INFO, "Vector Base: %s Interval: %.2f min (%u)",
			   atime, vector->interval / 60.0, vector->interval);

	crc16_cumul(NULL, 0);
	*(unsigned short *) crc_buf = htons(vector->seed);
	crc = crc16_cumul(crc_buf, 2);

	for (i = 0; i < WP_VECTOR_SIZE; i++)
		mycrc[i] = crc;

	vec_index = 0;
	treshold = vector->interval;
	dirty_first = 0;

/* DEBUG F6BVP */
	if (verbose) syslog(LOG_INFO,"FPACWPD: compute_vector() treshold = %d", treshold) ;

	for (i = 0; i <= db_header->nb_record; i++)
	{
		int last_done = (i == db_header->nb_record);
		int out_timeslot;

		if (!last_done)
		{
			out_timeslot =
				((vector->date_base > db_records[sorted_list[i]].date)
				 && (vector->date_base - db_records[sorted_list[i]].date >=
					 treshold));
/* DEBUG F6BVP 
			if (verbose) syslog(LOG_INFO,"out_timeslot =%d", out_timeslot);*/
		}

		if (last_done || out_timeslot)
		{
			if (dirty >= 0 && vector->crc[vec_index] != crc)
			{
				dirty_cnt[vec_index] = record_cnt[vec_index];
				if (!stop_dirty)
				{
					int k;
					for (k = dirty_first; k < i; k++)
					{
						set_dirty_context(sorted_list[k], dirty);
					}
					set_vector_when_nodirty(dirty);
					stop_dirty = 1;
				}
			}
			dirty_first = i;
			mycrc[vec_index] = crc;
			vec_index++;
/* DEBUG F6BVP 
		if (verbose) syslog(LOG_INFO,"vec_index =%d ", vec_index)*/;
			if (vec_index == (WP_VECTOR_SIZE - 1)) 
				treshold = 0xFFFFFFFF;
			else
				treshold = treshold * 2;
/* DEBUG F6BVP 
		if (verbose) syslog(LOG_INFO,"i:%d Index = %d / %d Treshold = %u / %u", i, vec_index, WP_VECTOR_SIZE, treshold, 0xFFFFFFFF);*/
			
			crc16_cumul(NULL, 0);
			*(unsigned short *) crc_buf = htons(vector->seed);
			crc16_cumul(crc_buf, 2);
		}

		if (!last_done)
		{
			char str[256];
			*(unsigned int *) crc_buf =
				htonl(db_records[sorted_list[i]].date);
			crc = crc16_cumul(crc_buf, 4);
			crc =
				crc16_cumul((unsigned char *) &db_records[sorted_list[i]].
							address, sizeof(struct full_sockaddr_rose));
			sprintf(str, "vect=%d call=%s crc=%u\n", vec_index,
					ax25_ntoa(&db_records[sorted_list[i]].address.srose_call),
					crc);
			record_cnt[vec_index] += 1;
		}
	}

	for (i = 0; i < WP_VECTOR_SIZE; i++)
	{
		vector->crc[i] = mycrc[i];
		vector->cnt[i] = record_cnt[i];
	}

	free(sorted_list);

	if (verbose)
	{
		char str[256];
		char *p = str;
		p += sprintf(p, "Vector Calculation (dirty/total):");
		for (i = 0; i < WP_VECTOR_SIZE; i++)
		{
			p += sprintf(p, " %d/%d", dirty_cnt[i], record_cnt[i]);
		}
		syslog(LOG_INFO, "%s", str);
	}

	return;
}
