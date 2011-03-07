/*
 * wp/fpacpwd.c : FPAC WP daemon
 *
 * FPAC project
 *
 * F1OAT 970831
 */
#include "config.h"
#include "wpdefs.h"
#include "sockevent.h"
#include "update.h"
#include "db.h"
#include "daemon.h"
#include "../pathnames.h"

static cfg_t cfg;		/* FPAC configuration file */
static int listening_socket;
static char *wp_file = FPACWP;	/* Default file */
int verbose = FALSE;
int wp_trace_flag = 0;
static int is_daemon = 1;
static int wp_passive = 0;
static int WPEDIT_CLIENT = 0;

/* WP contexts are indexed by socket handle */

struct wp_context *context[NB_MAX_HANDLES];
struct wp_adjacent *wp_adjacent_list = NULL;

/************************************************************************************
* Prototypes
************************************************************************************/

static void debug_dump(int s);
static void debug_pdu(wp_pdu *pdu);
static void rose_handler(int s);
static void do_cmd(int s, char *cmd);
static struct wp_adjacent *find_adjacent(rose_address *srose_addr);
static void vector_request(struct wp_adjacent *wpa);

/************************************************************************************
* Clients handler
************************************************************************************/

static int init_client(int client, struct full_sockaddr_rose *address)
{
	assert(context[client] == 0);
	context[client] = calloc(1, sizeof(*context[client]));
	if (!context[client]) {
		close(client);
		return -1;
	}
	context[client]->address = *address;
	
	if (strcmp("WP-0", ax25_ntoa(&address->srose_call)) == 0) {
		context[client]->type = WP_SERVER;
	}
	else {
		context[client]->type = WP_USER;
	}
	
	RegisterEventHandler(client, rose_handler);
	RegisterEventAwaited(client, READ_EVENT);	
	return 0;
}

static void close_client(int client, int active)
{
	if (verbose) {
		if (!active) {
			syslog(LOG_INFO, "(%d) Disconnected by client %s @ %s", client, ax25_ntoa(&context[client]->address.srose_call), rose_ntoa(&context[client]->address.srose_addr));
		}
		else {
			syslog(LOG_INFO, "(%d) Disconnecting client %s @ %s", client, ax25_ntoa(&context[client]->address.srose_call), rose_ntoa(&context[client]->address.srose_addr));
		}
	}
	if (context[client]->dirty_list) free(context[client]->dirty_list);
	close(client);
	UnRegisterEventAwaited(client, READ_EVENT);
	UnRegisterEventAwaited(client, WRITE_EVENT);
	if (context[client]->type == WP_SERVER && context[client]->adjacent) {
		struct wp_adjacent *wpa = context[client]->adjacent;
		wpa->state = WPA_DISCONNECTED;
		wpa->context = -1;
		wpa->retry_connect_date = time(NULL) + WPA_RETRY_CONNECT;
		context[client]->adjacent = NULL;
	}	
	
	free(context[client]);
	context[client] = NULL;
}


/************************************************************************************
* ROSE socket handler
************************************************************************************/

static void rose_write_handler(int s)
{
	int dirty;
	int rc; 
	struct wp_adjacent *wpa;
	wp_pdu pdu;

	assert(context[s]);
	wpa = context[s]->adjacent;
	
	if (wpa && (wpa->state == WPA_CONNECTING)) {
		if (verbose) syslog(LOG_INFO, "(%d) Connected to adjacent %s", s, rose_ntoa(&context[s]->address.srose_addr));
		wpa->state = WPA_CONNECTED;
		wpa->ismaster = 0;		
		wpa->vector_date = time(NULL);	
		wpa->vector_when_nodirty = 1;
	}
	
	dirty = find_dirty_context(s);
	if (dirty < 0) {
	    if (wpa && wpa->end_no_dirty && wpa->ismaster) {
			wp_pdu pdu;
			int rc;

	        memset (&pdu, 0, sizeof(wp_pdu));
		    /* If remote is master, client notifies the end of its wp updates */
			if (verbose) syslog(LOG_INFO, "Sending end_transaction adjacent %s", rose_ntoa(&context[s]->address.srose_addr));
			wpa->end_no_dirty = 0;
			pdu.type = wp_type_end_transaction;
			rc = wp_send_pdu(s, &pdu);
			if (rc) {
				close_client(s, 1);
			}
		}
		/*
		if (wpa && wpa->vector_when_nodirty) {
			if (verbose) syslog(LOG_INFO, "vector_request adjacent %s", rose_ntoa(&context[s]->address.srose_addr));
			wpa->vector_when_nodirty = 0;
			vector_request(wpa);
		}
		else */
		UnRegisterEventAwaited(s, WRITE_EVENT);
	}
	else {
		if (verbose) syslog(LOG_INFO, "Sending dirty record #%d %s to adjacent %s", 		 
			            dirty,
			            ax25_ntoa(&db_records[dirty].address.srose_call),
			            rose_ntoa(&context[s]->address.srose_addr));
		memset(&pdu, 0, sizeof(wp_pdu));
		pdu.type = wp_type_set;
		pdu.data.wp = db_records[dirty];		
		rc = wp_send_pdu(s, &pdu);
		if (rc) {
			if (verbose) syslog(LOG_INFO, "sending dirty record to adjacent FAILED ! rc %d", rc); 		 
			close_client(s, 1);
		}
		else {
			if (verbose) syslog(LOG_INFO, "dirty record sent to adjacent rc %d", rc); 		 
			clear_dirty_context(dirty, s);
		}	
	}
}

static void rose_read_handler(int s)
{
	int rc = 0;
	int again = 0;
	wp_pdu pdu;
	wp_t *wp_list;
	int i, nb;
	struct wp_info wpi;
	struct wp_adjacent *wpa;
	
	assert(context[s]);
	wpa = context[s]->adjacent;
	
	/* loop while data available */
	do {
	  memset(&pdu, 0, sizeof(wp_pdu));
	  rc = wp_receive_pdu(s, &pdu);
	  if (rc < 0) {	/* Connection lost or protocol error */
/* DEBUG F6BVP */		
		if (verbose) syslog(LOG_INFO, "rose_read_handler() receive_pdu rc %d connection lost or protocol error", rc);
		close_client(s, 0);
		return;
	  }
	  
	  again = (rc == 2);

	  switch (pdu.type) {
	  case wp_type_set:
		  /* For user client, check record validity */
		  /* if (context[s]->type != WP_USER || wp_valid(&pdu.data.wp)) {	*/

	  /* if client is WPEDIT-0 force = 1 */
		if (WPEDIT_CLIENT == 1) 
                  	rc = db_set(&pdu.data.wp, 1);
		else
			rc = db_set(&pdu.data.wp, context[s]->type != WP_USER);

		if (rc == -2 && verbose) {
			  syslog(LOG_INFO, "Invalid received record %s from adjacent %s",
				              ax25_ntoa(&pdu.data.wp.address.srose_call),
				              rose_ntoa(&context[s]->address.srose_addr));
			  rc = -1;
		  }
		  if (rc >= 0) {
			  pdu.data.status = WP_OK;
			  if (verbose) syslog(LOG_INFO, "rose_read_handler() wp_type_set status WP_OK %d", pdu.data.status);
			  /* Broadcast this new record */
			  broadcast_dirty(rc, s);
		  }
		  else {
			  pdu.data.status = WP_SET_ERROR;
			  if (verbose) syslog(LOG_INFO, "rose_read_handler() db_set -> rc %d status WP_SET_ERROR %d", rc, pdu.data.status);
		  }
	
		  if (context[s]->type == WP_USER)  {	/* Answer only for standard clients */
			  pdu.type = wp_type_response;
			  rc = wp_send_pdu(s, &pdu);
		  }
		  else {
			  if (verbose) syslog(LOG_INFO, "Receiving record %s from adjacent %s %s",
				              ax25_ntoa(&pdu.data.wp.address.srose_call),
				              rose_ntoa(&context[s]->address.srose_addr),
							  (rc >= 0) ? "Updated" : ((rc == -1) ? "Ignored" : "Error"));	
			rc = 0;
		  }
		  break;
	  case wp_type_get:
		  rc = db_get(&pdu.data.call, &pdu.data.wp);
		  if (rc < 0) {
			  pdu.data.status = WP_GET_ERROR;
			  pdu.type = wp_type_response;
			  if (verbose) syslog(LOG_ERR, "rose_read_handler() db_set -> rc %d status WP_GET_ERROR %d", rc, pdu.data.status);
		  }
		  else {
			  pdu.type = wp_type_get_response;
		  }
		  rc = wp_send_pdu(s, &pdu);
		  wp_flush_pdu();
		  break;
	  case wp_type_info:
		  db_info(&wpi);
		  pdu.type = wp_type_info_response;
		  pdu.data.info_rsp.mask  = pdu.data.info.mask;
		  pdu.data.info_rsp.nbrec = wpi.nbrec;
		  pdu.data.info_rsp.size  = wpi.size;
		  rc = wp_send_pdu(s, &pdu);
		  wp_flush_pdu();
		  break;
	  case wp_type_get_list:
		  rc = db_list_get(&pdu.data.list_req, &wp_list, &nb);
		  if (rc < 0) {
			  pdu.data.status = WP_GET_ERROR;
			  pdu.type = wp_type_response;
			  if (verbose) syslog(LOG_ERR, "rose_read_handler() db_list_get -> rc %d status WP_GET_ERROR %d", rc, pdu.data.status);
			  rc = wp_send_pdu(s, &pdu);
			  break;
		  }
		  if (nb == 0) {
			  pdu.data.status = WP_OK;
			  pdu.type = wp_type_response;
			  rc = wp_send_pdu(s, &pdu);
		  }
		  else {
			  for (i = 0 ; i < nb ; i++) {
				  pdu.type = wp_type_get_list_response;
				  pdu.data.list_rsp.wp   = wp_list[i];
				  pdu.data.list_rsp.pos  = i;
				  pdu.data.list_rsp.next = ((i+1) != nb);
				  rc = wp_send_pdu(s, &pdu);
			  }
		  }
		  wp_flush_pdu();
		  db_list_free(&wp_list);
		  break;
	  case wp_type_ascii:
		  do_cmd(s, pdu.data.string);
		  wp_flush_pdu();
		  break;
	  case wp_type_vector_request:
		  if (pdu.data.vector.version != WP_VERSION) {
			  if (verbose) syslog(LOG_INFO, "Receiving vector request from %s : Bad version number %02x", rose_ntoa(&context[s]->address.srose_addr), pdu.data.vector.version);
			  rc = -1;
		  }
		  else {
			  if (verbose) syslog(LOG_INFO, "Receiving vector request from %s", rose_ntoa(&context[s]->address.srose_addr));
			  db_compute_vector(s, &pdu.data.vector);
/*DEBUG F6BVP */  if (verbose) debug_pdu(&pdu);
			  pdu.type = wp_type_vector_response;
			  if (verbose) syslog(LOG_INFO, "Sending vector response to %s", rose_ntoa(&context[s]->address.srose_addr));
			  rc = wp_send_pdu(s, &pdu);
		  }
		  break;
	  case wp_type_vector_response:
		  if (pdu.data.vector.version != WP_VERSION) {
			  if (verbose) syslog(LOG_INFO, "Receiving vector response from %s : Bad version number %02x", rose_ntoa(&context[s]->address.srose_addr), pdu.data.vector.version);
			  rc = -1;
		  }
		  else {
			  if (verbose) syslog(LOG_INFO, "Receiving vector response from %s", rose_ntoa(&context[s]->address.srose_addr));
			  db_compute_vector(s, &pdu.data.vector);
/*DEBUG F6BVP */  if (verbose) debug_pdu(&pdu);
		  }
		  break;
	  case wp_type_end_transaction:
		  if (wpa && wpa->vector_when_nodirty) {
			  if (verbose) syslog(LOG_INFO, "Receiving end_transaction adjacent %s", rose_ntoa(&context[s]->address.srose_addr));
			  wpa->vector_when_nodirty = 0;
			  vector_request(wpa);
		  }
		  break;
	  default:
		  pdu.type = wp_type_response;
		  pdu.data.status = WP_INVALID_COMMAND;
		  rc = wp_send_pdu(s, &pdu);
		  break;
	  }
	} while (again);
	
	if (rc) {
		close_client(s, 1);
	}
}

static void rose_handler(int s)
{
	if (GetEvent(s) & (1<<READ_EVENT)) rose_read_handler(s);
	else if (GetEvent(s) & (1<<WRITE_EVENT)) rose_write_handler(s);
}

static void listening_handler(int s)
{
	int new_client;
	struct full_sockaddr_rose address;
	unsigned int addrlen = sizeof(address);
	struct full_sockaddr_rose l_address;
/*	struct wp_adjacent *wpa;*/
	unsigned int l_addrlen = sizeof(l_address);
/*	char *fulladdr;*/
/*	node_t *node;*/

	new_client = accept(s, (struct sockaddr *)&address, &addrlen);

	if (strncmp(ax25_ntoa(&address.srose_call),"WPEDIT-0",8) == 0)
		WPEDIT_CLIENT = 1;

	if (new_client < 0) return;
	if (verbose) syslog(LOG_INFO, "New client %s @ %s", ax25_ntoa(&address.srose_call), rose_ntoa(&address.srose_addr));
	
	if (init_client(new_client, &address)) return;
	
	if (context[new_client]->type == WP_SERVER) {
		struct wp_adjacent *wpa;
		wpa = find_adjacent(&address.srose_addr);
		if (wpa) {
			if (verbose) syslog(LOG_INFO, "New client is adjacent %s", wpa->node->name);
			switch (wpa->state) {
			case WPA_CONNECTED:
				if (verbose) syslog(LOG_INFO, "Already connected to adjacent %s", wpa->node->name);
				close_client(new_client, 1);
				break;
			case WPA_CONNECTING:
				assert(!getsockname(listening_socket, (struct sockaddr *)&l_address, &l_addrlen));
				/* Crossed connection requests : higher calling address win */
				if (memcmp(&l_address.srose_addr, &address.srose_addr, sizeof(address.srose_addr)) > 0) {
					if (verbose) syslog(LOG_INFO, "Already connecting to adjacent %s", wpa->node->name);
					close_client(new_client, 1);
					break;
				}
				else {
					close_client(wpa->context, 1);
				}
				/* Here, there is no break !!! */
			case WPA_DISCONNECTED:
				context[new_client]->adjacent = wpa;
				wpa->context = new_client;
				wpa->state = WPA_CONNECTED;
				wpa->ismaster = 1;
				wpa->vector_date = time(NULL) + WPA_VECTOR_PERIOD;
				RegisterEventAwaited(new_client, WRITE_EVENT);
				break;			
			}
		}
		else {
			node_t *node;
			
			if (verbose) syslog(LOG_INFO, "New client is unknown");
			wpa = calloc(sizeof(*wpa), 1);
			node = calloc(sizeof(*node), 1);
			
			if (!wpa || !node) {
				syslog(LOG_ERR, "listening_handler: Out of memory");
				close_client(new_client, 1);
			}
			else {
				char *fulladdr;
				
				wpa->next = wp_adjacent_list;
				wp_adjacent_list = wpa;	
				wpa->is_unknown = 1;
				wpa->node = node;
				
				fulladdr = rose_ntoa(&address.srose_addr);
				strncpy(node->dnic, fulladdr, 4);
				node->dnic[4] = 0;
				strncpy(node->addr, fulladdr+4, 6);
				node->addr[6] = 0;
				strcpy(node->name, "???");
				strcpy(node->call, "???");
					
				context[new_client]->adjacent = wpa;
				wpa->context = new_client;
				wpa->state = WPA_CONNECTED;
				wpa->ismaster = 1;
				wpa->vector_date = time(NULL) + WPA_VECTOR_PERIOD;
				RegisterEventAwaited(new_client, WRITE_EVENT);				
			}	
		}
	}
}

static int init_rose(void)
{	
	listening_socket = wp_listen();
	if (listening_socket < 0) return -1;
	
	RegisterEventHandler(listening_socket, listening_handler);
	RegisterEventAwaited(listening_socket, READ_EVENT);
	return 0;	
}

/************************************************************************************
* Adjacents functions 
************************************************************************************/

static int init_adjacents(cfg_t *cfg)
{
	node_t *node;
	struct wp_adjacent *wpa;
		
	for (node=cfg->node; node; node=node->next) {
		if (node->nowp) continue;
		wpa = calloc(sizeof(*wpa), 1);
		if (!wpa) {
			syslog(LOG_ERR, "init_adjacents: Out of memory");
			return -1;
		}
		
		wpa->state = WPA_DISCONNECTED;
		wpa->node = node;
		wpa->context = -1;
		wpa->retry_connect_date = time(NULL);
		wpa->next = wp_adjacent_list;
		wp_adjacent_list = wpa;
	}
	
	return 0;
}

static struct wp_adjacent *find_adjacent(rose_address *srose_addr)
{
	char *fulladdr;
	char dnic[5], addr[7];
	struct wp_adjacent *wpa;
	
	fulladdr = rose_ntoa(srose_addr);
	strncpy(dnic, fulladdr, 4);
	dnic[4] = 0;
	strncpy(addr, fulladdr+4, 6);
	addr[6] = 0;

	for (wpa=wp_adjacent_list; wpa; wpa=wpa->next) {
		if (!wpa->node) continue;
		if (!strcmp(addr, wpa->node->addr) && !strcmp(dnic, wpa->node->dnic)) {
			if (verbose) syslog(LOG_INFO, "wp_adjacent DNIC,address %s,%s name : %s", wpa->node->dnic, wpa->node->addr, wpa->node->name); 
			return wpa;
		}
	}
	
	return NULL;	
}

static void connect_adjacent(struct wp_adjacent *wpa)
{
	int s;
	char addr[11];
	struct full_sockaddr_rose remote;

	if (!wpa->node) return;
	
	strcpy(addr, wpa->node->dnic);
	strcat(addr, wpa->node->addr);
		
	if (verbose) syslog(LOG_INFO, "Trying to connect adjacent %s", addr);

	wpa->retry_connect_date = time(NULL) + 120;

	remote.srose_family = AF_ROSE;
	remote.srose_ndigis = 0;
	ax25_aton_entry("WP", remote.srose_call.ax25_call);
	rose_aton(addr, remote.srose_addr.rose_addr);
	s = wp_open_remote("WP", &remote, 1);	/* Non blocking mode */
	if (s < 0) {
		perror("wp_open_remote");
		return;
	}
	init_client(s, &remote);
	context[s]->adjacent = wpa;
	wpa->context = s;
	RegisterEventAwaited(s, WRITE_EVENT);
	wpa->state = WPA_CONNECTING;	
}

static void vector_request(struct wp_adjacent *wpa)
{
	wp_pdu pdu;
	vector_t vector;
	int s = wpa->context;
	int rc;
			
	wpa->vector_date = time(NULL) + WPA_VECTOR_PERIOD;

	memset(&pdu, 0, sizeof(wp_pdu));
	pdu.type = wp_type_vector_request;

	vector.version = WP_VERSION;
	vector.date_base = 0;
	vector.seed = random();
	db_compute_vector(-1, &vector);
	pdu.data.vector = vector;
	if (verbose) syslog(LOG_INFO, "Sending vector request to %s", rose_ntoa(&context[s]->address.srose_addr));
/*DEBUG F6BVP */  if (verbose) debug_pdu(&pdu);
	rc = wp_send_pdu(s, &pdu);
	if (rc < 0) {
		if (verbose) syslog(LOG_INFO, "Sending vector request FAILED !");
		close_client(s, 1);
	}
	else
		if (verbose) syslog(LOG_INFO, "vector request sent ! rc %d", rc);
}

static void poll_adjacents(void)
{
	struct wp_adjacent *wpa;
	time_t mytime = time(NULL);
	
	for (wpa=wp_adjacent_list; wpa; wpa=wpa->next) {
		switch (wpa->state) {
		case WPA_CONNECTED:
			if (mytime >= wpa->vector_date) {
				vector_request(wpa);
			}			
			break;
		case WPA_CONNECTING:
			break;
		case WPA_DISCONNECTED:
			if (!wpa->is_unknown && mytime >= wpa->retry_connect_date) {
				if (!wp_passive) { /* Debug node is in passive mode */
					connect_adjacent(wpa);
				}
			}
			break; 
		}
	}

}

/************************************************************************************
* Debug dump handler 
************************************************************************************/

static void printf_pdu(int s, char *fmt, ...)
{
	wp_pdu pdu;
	va_list ap;
	
	va_start(ap, fmt);
	memset(&pdu, 0, sizeof(wp_pdu));
	vsnprintf(pdu.data.string, sizeof(pdu.data.string), fmt, ap);
	pdu.type = wp_type_ascii;
	wp_send_pdu(s, &pdu);
	
	va_end(ap);	
}

/*
static void record_dump(int index)
{
	printf(ax25_ntoa(&db_records[index].address.srose_call));
}
*/
static void debug_dump(int s)
{
	char str[256];
	char tmp[80];
	char type[40];
	int i;
	struct wp_adjacent *wpa;
	struct wp_info wpi;
	vector_t vector;
	
	printf_pdu(s, "WP Server version %d.%d\r", WP_VERSION >> 8, WP_VERSION & 0xff);
	
	db_info(&wpi);
	printf_pdu(s, "Node %s @ %s,%s\r", cfg.callsign, cfg.dnic, cfg.address);
	printf_pdu(s, "Database (records) : size=%d used=%d\r", wpi.size, wpi.nbrec);
	
	printf_pdu(s, "\r");
	printf_pdu(s, "Client Callsign  Address     Type   Dirty\r");
	
	for (i=0; i<NB_MAX_HANDLES; i++) {
		
		if (!context[i]) continue;
		
		switch (context[i]->type) {
		case WP_USER :
			strcpy(type, "User");
			break;
		case WP_SERVER :
			strcpy(type, "Server");
			break;
		default :
			strcpy(type, "????");
			break;
		}

		printf_pdu(s, "%6d %-9s %s %-6s %d\r", 
			i, 
			ax25_ntoa(&context[i]->address.srose_call),
			fpac2asc(&context[i]->address.srose_addr),
			type,
			count_dirty_context(i));
	}

	printf_pdu(s, "\r");
	printf_pdu(s, "Adjacent-Name    Callsign  Address      State\r");

	for (wpa=wp_adjacent_list; wpa; wpa=wpa->next) {
		sprintf(str, "%-16s %-9s %s,%s ", 
			wpa->node->name, wpa->node->call, wpa->node->dnic, wpa->node->addr);
		switch (wpa->state) {
		case WPA_CONNECTED:
			printf_pdu(s, "%s connected %s #%d #dirty=%d\r", 
				str,
				(wpa->ismaster) ? "master" : "slave ",
				wpa->context,
				count_dirty_context(wpa->context));
			break;
		case WPA_CONNECTING:
			printf_pdu(s, "%s connection in progress\r", str);
			break;
		case WPA_DISCONNECTED:
			printf_pdu(s, "%s disconnected, retry in %d s\r", str,
				wpa->retry_connect_date - time(NULL));
			break; 
		}
	}

	printf_pdu(s, "\r");
	
	printf_pdu(s, "Vector structure\r");
	vector.version = WP_VERSION;
	vector.date_base = 0;
	vector.seed = 0;
	db_compute_vector(-1, &vector);
	
	sprintf(tmp, "%s", ctime(&vector.date_base));
	tmp[strlen(tmp)-1] = '\0';
	printf_pdu(s, "Date:%s\r", tmp);
	printf_pdu(s, "Total:%d  Interv:%.2f min (%u)\r", wpi.nbrec, vector.interval/60.0, vector.interval);

	str[0] = '\0';
	for (i=0 ; i<WP_VECTOR_SIZE ; i++) {
		sprintf(tmp, "%02d:%04x/%-5d", i, vector.crc[i], vector.cnt[i]);
		strcat(str, tmp);
		if (((i+1) % 4) == 0) {
			strcat(str, "\r");
			printf_pdu(s, str);
			str[0] = '\0';
		}
		else
			strcat(str, " ");
	}

	printf_pdu(s, "=== End ===\r");
}

static void debug_pdu(wp_pdu *pdu)
{
	char str[256];
	char tmp[80];
	int i, j=0;
	
	for (i=0 ; i<WP_VECTOR_SIZE ; i++) 
		j += pdu->data.vector.cnt[i];

	syslog(LOG_INFO, "Vector structure :");
	sprintf(tmp, "%s", ctime(&pdu->data.vector.date_base));
	tmp[strlen(tmp)-1] = '\0';
	syslog(LOG_INFO, "Date %s ", tmp);
	syslog(LOG_INFO, "vector version %u seed %u Total %d Interv:%.2f min (%u)",
		       	pdu->data.vector.version, pdu->data.vector.seed, j, pdu->data.vector.interval/60.0, pdu->data.vector.interval);
	str[0] = '\0';
	for (i=0 ; i<WP_VECTOR_SIZE ; i++) {
		sprintf(tmp, "%02d:%05d/%-5d", i, pdu->data.vector.crc[i], pdu->data.vector.cnt[i]);
		strcat(str, tmp);
		if (((i+1) % 4) == 0) {
			strcat(str, "\r");
			syslog(LOG_INFO, "%s", str);
			str[0] = '\0';
		}
		else
			strcat(str, " ");
	}
	syslog(LOG_INFO, "%s", str);

}

static void do_cmd(int s, char *cmd)
{
	debug_dump(s);
}
 
/************************************************************************************
* Main entry point 
************************************************************************************/

static void Usage(void)
{
	fprintf(stderr, "Usage : fpacwpd [-h] [-d] [-x] [-v] [-f wpfile]\n");
	fprintf(stderr, "-h : display this message\n");
	fprintf(stderr, "-d : start in foreground mode\n");
	fprintf(stderr, "-x : turn on debug mode\n");
	fprintf(stderr, "-v : turn on debug verbose mode\n");
	fprintf(stderr, "-p : turn on passive mode\n");
	fprintf(stderr, "-f wpfile : specify another wp file (default %s)\n", wp_file); 
	exit(1);
}

static void process_options(int argc, char *argv[])
{
	int c;
	
	do {
		c = getopt(argc, argv, "hdvxpf:");
		switch (c) {
		case 'h':
		case '?':
			Usage();
			break;
		case 'd':
			fprintf(stderr, "Foreground mode\n");
			is_daemon = 0;
			verbose = 1;
			break;
		case 'x':
			fprintf(stderr, "Debug mode\n");
			wp_debug = 1;
			break;
		case 'v':
			fprintf(stderr, "Verbose mode\n");
			verbose = TRUE;
			break;		
		case 'p':
			fprintf(stderr, "Passive mode\n");
			wp_passive = 1;
			break;		
		case 'f':
			wp_file = optarg;
			fprintf(stderr, "Using file %s\n", wp_file);
			break;
		}
	} while (c != EOF);
}

int main(int argc, char *argv[]) 
{
	int rc;
	int fd;
	wp_t *wp;
	
	process_options(argc, argv);
	
	openlog("fpacwpd", LOG_PERROR | LOG_PID, LOG_USER);	
	syslog(LOG_WARNING, "Starting version %s - vector version %x Hex (%d dec) - file signature %s",
				VERSION, WP_VERSION, WP_VERSION, FILE_SIGNATURE);
	srand(time(NULL));
	
	if (cfg_open(&cfg) != 0) {
		perror("fpacwpd : error in configuration reading");
		exit(1);
	}
	
	if (init_adjacents(&cfg)) {
		perror("fpacwpd : error init adjacents");
		exit(1);
	}
		
	rc = init_rose();
	if (rc < 0) {
		syslog(LOG_ERR, "fpacwpd : cannot init ROSE access point");
		exit(1);
	}
	
	/* Informations of the node in case of creation of database */
	/*memset(&wp, 0, sizeof(wp));*/
	wp = calloc(sizeof(*wp), 1);

	wp->date = time(NULL);
	wp->is_node = 1;
	wp->address.srose_ndigis = 0;
	strcpy(wp->city, cfg.city);
	strcpy(wp->locator, cfg.locator);
	ax25_aton_entry(cfg.alt_callsign, wp->address.srose_call.ax25_call);
	rose_aton(cfg.fulladdr, wp->address.srose_addr.rose_addr);

	rc = db_open(wp_file, wp);
	if (rc) {
		fprintf(stderr, "fpacwpd : cannot open database %s\n", wp_file);
		exit(1);	
	}
		
	if (is_daemon && !daemon_start(TRUE)) {
		fprintf(stderr, "fpacwpd cannot become daemon\n");
		exit(1);
	}
		
	while (1) {
/*		fd = WaitEvent(1000);*/
/* wait 2 seconds = 2000 milliseconds */
		fd = WaitEvent(2000);
		if (fd >= 0) {
			ProcessEvent(fd);
		}
		else {
			poll_adjacents();
		}
	}
return 0;
}
