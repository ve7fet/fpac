/*
 * wp/hash.c
 *
 * FPAC project
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "hash.h"
#include "crc.h"

typedef struct {
	int		hash_bits;
	unsigned char	**record_table;
	int		record_size;
	int		key_offset;
	int		key_size;
	unsigned short	*table;
	unsigned short	*list;
	int		list_size;
} hash_t;

#define NB_MAX_HASH	16
static hash_t *hash[NB_MAX_HASH];

#define HASH_CHUNK	256

/*****************************************************************************
* Public functions section
*****************************************************************************/

int hash_open(int hash_bits, int record_size, int key_offset, int key_size, void **record_table)
{
	int h;
	hash_t *ht;
	
	for (h=0; h<NB_MAX_HASH; h++) {
		if (!hash[h]) break;
	}
	
	if (h == NB_MAX_HASH) return -1;
	
	ht = calloc(1, sizeof(hash_t));
	if (!ht) return -1;
	
	ht->hash_bits = hash_bits;
	ht->record_size = record_size;
	ht->key_offset = key_offset;
	ht->key_size = key_size;
	ht->record_table = (unsigned char **)record_table;
	ht->table = calloc(1<<hash_bits, sizeof(unsigned short));
	if (!ht->table) {
		free(ht);
		return -1;
	}
	
	hash[h] = ht;
	return h;
}

int hash_close(int handle) 
{
	return -1;
}

int hash_put(int handle, unsigned short index)
{
	hash_t *ht;
	unsigned short hash_code;
	unsigned short *linked_list;
	unsigned char *key; /* *other_key;*/
	unsigned short *new_list;
/*	int rc, new_size;*/

	assert(handle >= 0 && handle < NB_MAX_HASH && hash[handle]);
	ht = hash[handle];
	 
	/* If no more room in list, increase list size */
	
	if (index >= ht->list_size) {
 		int new_size = index+HASH_CHUNK;
		new_list = realloc(ht->list, new_size*sizeof(ht->list[0]));
		if (!new_list) return -1;
		ht->list = new_list;
		memset(&ht->list[ht->list_size], 0, 
		       sizeof(ht->list[0])*(new_size-ht->list_size));
		ht->list_size = new_size;
	}
	
	/* Compute hash code */
	
	key = *ht->record_table + index*ht->record_size + ht->key_offset;
	hash_code = crc16(key, ht->key_size);
	hash_code &= (1<<ht->hash_bits)-1;
	
	/* Index this record */
	
	linked_list = &ht->table[hash_code];
	if (!*linked_list) {	/* Entry is free */
		ht->table[hash_code] = index+1;
		ht->list[index] = 0;
	} 
	else {	/* Entry is not free, put in sorted order */
		unsigned char *other_key;
		int rc;
		
		while (*linked_list) {			
			other_key = *ht->record_table + 
				   (*linked_list-1)*ht->record_size + 
				   ht->key_offset;
			if (*linked_list-1 == index) break;	/* Already indexed */
			rc = memcmp(key, other_key, ht->key_size);
			if (rc == 0) {
				/* This key is duplicated - The record must be deleted */
				return -2;
			}
			assert(rc);
			if (rc == 1) break;
			linked_list = &ht->list[*linked_list-1];
		}
		
		if (*linked_list-1 != index) {
			ht->list[index] = *linked_list;
			*linked_list = index+1;
		}
	}
	
	return 0;
}

int hash_search(int handle, void *key)
{
	hash_t *ht;
	unsigned short hash_code;
	unsigned short *linked_list;
	unsigned char *other_key;	
	
	assert(handle >= 0 && handle < NB_MAX_HASH && hash[handle]);
	ht = hash[handle];
	 	
	/* Compute hash code */
	
	hash_code = crc16(key, ht->key_size);
	hash_code &= (1<<ht->hash_bits)-1;
	
	/* Gest linked list associated to this hash_code */
	
	linked_list = &ht->table[hash_code];
	if (!*linked_list) {	/* No record associated */
		return -1;
	} 
	else {	/* Look for good record */
		
		while (*linked_list) {			
			other_key = *ht->record_table + 
				   (*linked_list-1)*ht->record_size + 
				   ht->key_offset;
			if (memcmp(key, other_key, ht->key_size) == 0) {
				/* Record found */
				return *linked_list-1;
			}
			linked_list = &ht->list[*linked_list-1];
		}
		return -1;
	}
}

void hash_dump(int handle, void (*record_dump)(int index))
{
	int hash_code;
	unsigned short *linked_list;
	hash_t *ht;

	assert(handle >= 0 && handle < NB_MAX_HASH && hash[handle]);
	ht = hash[handle];
		
	for (hash_code=0; hash_code<(1<<ht->hash_bits); hash_code++) {
		linked_list = &ht->table[hash_code];
		if (!*linked_list) continue;
		printf("0x%04X: ", hash_code);
		while (*linked_list) {			
			record_dump(*linked_list-1);
			printf(" ");
			linked_list = &ht->list[*linked_list-1];
		}
		printf("\n");
	}
}

#if 0
int hash_signature(int handle, int mask, int line, unsigned short seed)
{
	int hash_code;
	hash_t *ht;

	assert(handle >= 0 && handle < NB_MAX_HASH && hash[handle]);
	ht = hash[handle];
	
	/* Init CRC generator with seed */
	
	seed = htons(seed);
	crc16_cumul(NULL, 0);
	crc16_cumul(seed, sizeof(seed));
	
	/* Compute signature according to bitmask */
	 	
	for (hash_code=0; hash_code<(1<<ht->hash_bits); hash_code++) {
		linked_list = &ht->table[hash_code];
		if (!*linked_list) continue;
		printf("0x%04X: ", hash_code);
		while (*linked_list) {			
			record_dump(*linked_list-1);
			printf(" ");
			linked_list = &ht->list[*linked_list-1];
		}
		printf("\n");
	}	
}
#endif
