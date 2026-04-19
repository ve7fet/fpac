#include <malloc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
/*#include <sys/types.h>*/
/*#include <sys/socket.h>*/
/*#include <sys/fcntl.h>*/
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <unistd.h>
/*#include <netdb.h>*/
#include <time.h>
#include <signal.h>
#include <stdarg.h>
#include <syslog.h>

#include "ax25compat.h"
#include "wp.h"
#include "fpac.h"

#define WP_VERSION	0x111	/* was 0x107 - fpacwpd2.dat - FPACWP_V002 signature */
/* use strerror or strerror_r */
/*#define perror(s)	syslog(LOG_ERR, "%s:%s", s, strerror)*/
#define perror(s)	syslog(LOG_ERR, "%s", s)
#define TRACE(s)	if (wp_trace_flag) wp_trace s;
#define MIN(a,b)	((a) < (b) ? (a) : (b))

#define NEW_RECORD_CHUNK	128	/* Number of records allocated in one time */
#define HASH_BITS		10
#define NB_MAX_HANDLES		128	/* Max number of file handle (one per connection) */

struct wp_adjacent {
	int	state;
	int	ismaster;	/* True if this adjacent is master */
	int 	is_unknown;	/* True if this adjacent is not declared in fpac.conf */
	node_t	*node;
	int	context;
	time_t	retry_connect_date;
	time_t	vector_date;
	int	vector_when_nodirty;	/* Do a new vector request when no more dirty records */
	int	end_no_dirty;	/* client has sent all its dirty records */
	struct wp_adjacent *next;
};

#define WPA_DISCONNECTED	0
#define WPA_CONNECTING		1
#define WPA_CONNECTED		2

#define WPA_RETRY_CONNECT	(30*60)	/* Retry adjacent connection delay */
#define WPA_VECTOR_PERIOD	(3600)	/* Process a vector exchange every hour */	

/* #define WP_API_TIMEOUT	10	Timeout for access to wp server */
#define PROC_RS_FILE		"/proc/net/rose"

/* Clients and remote WP-servers structure */

struct wp_context {
	int			type;
	struct full_sockaddr_rose 	address;
	unsigned char 		*dirty_list;
	int 			dirty_size;
	struct wp_adjacent	*adjacent;
};

struct wp_info {
	int		size;
	int 	nbrec;
};

#define WP_USER			0
#define WP_SERVER		1

extern int verbose;
extern struct wp_context *context[NB_MAX_HANDLES];
extern struct wp_adjacent *wp_adjacent_list;
extern int wp_trace_flag, wp_debug, verbose;

