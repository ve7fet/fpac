
/******************************************************
 *                                                    *
 * FPAC project.            FPAC PAD                  *
 *                                                    *
 * Parts of code from different sources of ax25-utils *
 *                                                    *
 * F6FBB 05-1997                                      *
 *                                                    *
 ******************************************************/

#include "fpac.h"
#include <netdb.h>

#define IPC_KEY 0x6fbb

/* Address Port */
typedef struct addport_d
{
	char name[20];
	int fd;
	char addr[14];
	char port[10];
	struct addport_d *next;
} addport_t;

typedef struct buf
{
	char *data;
	int len;
	struct buf *next;
} buf_t;

typedef struct user_d
{
	int fd;
	int queue;
	int type;
	int state;
	int verbose;
	char user[10];
	buf_t *dataq;
	struct user_d *peer;
	struct user_d *next;
} user_t;

/*
 * Function prototypes
 */

extern int read_conf(int verbose);
extern int add_wp(struct full_sockaddr_rose *addr);
extern int search_wp(ax25_address *call, struct full_sockaddr_rose *addr);
extern int wp_init(int verbose);
extern int conf_changed(void);

extern char *l3_attach(char *rsaddr, struct hostent *hp, int verbose);
extern char *user_port(ax25_address *);

extern void dump_ax25(char *title, struct full_sockaddr_ax25 *ax25);
extern void dump_rose(char *title, struct full_sockaddr_rose *rose);


/*
 * Global variables
 */
/*
extern	char fpac_call[20];
extern	char fpac_digi[20];
extern	char fpac_dnic[20];
extern	char fpac_addr[20];
extern	char fpac_cover[80];
extern	char fpac_dev[64];
extern	char node_addr[20];
extern	char ax25_port[256];
extern	char def_port[64];
*/
extern	cfg_t cfg;
/*
extern	node_t  *node_head;
extern	luser_t *luser_head;
extern	alias_t *alias_head;
extern	route_t *route_head;
extern	port_t	*port_head;
extern	addport_t *addp_head;
*/
