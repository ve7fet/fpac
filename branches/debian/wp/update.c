/*
 * wp§update.c
 *
 * FPAC project
 *
 * WP inter-nodes update procedures
 *
 * F1OAT 980117
 */

#include "wpdefs.h"
#include "sockevent.h"
#include "update.h"
#include "db.h"
#include "daemon.h"

/************************************************************************************
* Dirty bits handler
************************************************************************************/

int set_dirty_context(int record, int client)
{
	int byte_index;
	unsigned char *newlist;
	
	assert(context[client]);
	
	byte_index = record/8;
	if (byte_index >= context[client]->dirty_size) {
		newlist = realloc(context[client]->dirty_list, 1+db_header->nb_record/8);
		if (!newlist) return -1;
		memset(newlist+context[client]->dirty_size, 0, 1+db_header->nb_record/8 - context[client]->dirty_size);
		context[client]->dirty_list = newlist;
		context[client]->dirty_size = 1+db_header->nb_record/8;
	}
	
	assert(byte_index < context[client]->dirty_size);
	
	context[client]->dirty_list[byte_index] |= (1 << (record%8));
	
	RegisterEventAwaited(client, WRITE_EVENT);
	return 0;
}

int clear_dirty_context(int record, int client)
{
	int byte_index;
	
	byte_index = record/8;
	assert(byte_index < context[client]->dirty_size);
	context[client]->dirty_list[byte_index] &= ~(1 << (record%8));
	
	return 0;
}

int find_dirty_context(int client)
{
	int i;
	int record;
	unsigned char k;

	if (!context[client]->dirty_size) return -1;
	
	for (i=0; i<context[client]->dirty_size; i++) {
		if (context[client]->dirty_list[i] != 0) {
			record = 8*i;
			k = context[client]->dirty_list[i];
			while (! (k & 1) ) {
				record++;
				k = k >> 1;
			}
			return record;
		}
	}
	
	return -1;
}

int count_dirty_context(int client)
{
	int i, count = 0;;
	unsigned char k;

	if (!context[client]->dirty_size) return 0;
	
	for (i=0; i<context[client]->dirty_size; i++) {
		if (context[client]->dirty_list[i] != 0) {
			k = context[client]->dirty_list[i];
			while (k) {
				if (k & 1) count++;
				k = k >> 1;
			}
		}
	}
	
	return count;
}

void broadcast_dirty(int record, int except)
{
	int client;
	
	for (client=0; client<NB_MAX_HANDLES; client++) {
		if (client == except) continue;
		if (context[client] && context[client]->type == WP_SERVER) {
			set_dirty_context(record, client);
		}
	}
}

void set_vector_when_nodirty(int client)
{
	if (context[client]->adjacent) {
		if (context[client]->adjacent->ismaster)
			context[client]->adjacent->end_no_dirty = 1;
		else
			context[client]->adjacent->vector_when_nodirty = 1;
	}
}
 
