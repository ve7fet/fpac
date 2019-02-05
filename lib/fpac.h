
/******************************************************
 * lib/fpac.h                                         *
 * FPAC project.           CONFIGURATION INCLUDE FILE *
 *                                                    *
 * Parts of code from different sources of ax25-utils *
 *                                                    *
 * F6FBB 09-1997                                      *
 *                                                    *
 ******************************************************/


#ifndef __FPAC_H
#define __FPAC_H

#define FPACNODE "fpacnode"

#include <sys/time.h>

#include "../pathnames.h"

typedef struct dniclu_d
{
	long country;
	int	dnic;
} dniclu_t;

/* Adjacent node */
typedef struct node_d
{
	char name[20];
	char call[80];
	char dnic[5];
	char addr[7];
	char port[10];
	int nowp;
	struct node_d *next;
} node_t;

#define MAXCOVER	64	/* Maximum number of adjacent nodes	*/

/* Local user */
typedef struct luser_d
{
	char name[20];
	char call[80];
	char port[10];
	struct luser_d *next;
} luser_t;

/* Alias */
typedef struct alias_d
{
	int fd;
	char alias[10];
	char path[80];
	struct alias_d *next;
} alias_t;

/* Address Port */
typedef struct addrp_d
{
	char name[20];
	int fd;
	char addr[14];
	char port[10];
	struct addrp_d *next;
} addrp_t;

/* Application */
typedef struct appli_d
{
	int fd;
	char call[10];
	char appli[256];
	struct appli_d *next;
} appli_t;

/* Structure of a port */
typedef struct port_d
{
	char name[20];
	int fd_call;
	int fd_digi;
	int fd_appli;
	alias_t *alias;
	appli_t *applis;
	struct port_d *next;
} port_t;

/* Routes */
typedef struct route_d
{
	char addr[14];
	char nodes[80];
	struct route_d *next;
} route_t;

typedef struct cover_d
{
	char addr[7];
	struct cover_d *next;
} cover_t;

typedef struct cmd_d {
	char	name[20];
	char	*cmd;
	struct cmd_d *next;
} cmd_t;

typedef struct
{
	time_t	date;			/* Date of file */
	char	callsign[10];	/* Callsign of the node */
	char	alt_callsign[10];/* Alternate callsign of the node */
	char	trt_callsign[10];/* TraceRoute callsign of the node */
	char	city[22];		/* City of the node */
	char	state[22];		/* State of the node */
	char	country[22];		/* Country of the node */
	char	locator[7];		/* Qth locator of the node */
	char	dnic[5];		/* DNIC of the node */
	char	address[7];		/* Default address of the node */
	char	fulladdr[11];	/* Node full address */
	char	def_port[20];	/* Default port */
	char	password[243];	/* Password */
	char	option[20];		/* Champ option */
	node_t	*node;			/* Head of the adjacent nodes list */
	alias_t	*alias;			/* Head of the alias callsigns list */
	luser_t	*luser;			/* Head of users list */
	addrp_t *addrp;			/* Head of address of ports */
	route_t	*route;			/* Head of routes list */
	cover_t	*cover;			/* Head of coverage list */
	port_t	*port;			/* Head of ports list */
	appli_t	*appli;			/* Head of application list */
	cmd_t	*cmd;			/* Head of commands list */
	cmd_t	*syscmd;		/* Head of sysop commands list */
	int	inetport;			/* TCP port */
	char	def_addr[80];	/* IP address ports */
} cfg_t;

/* Public functions provided by libfpac.a */

extern char *des2dnic(char *des);
extern char *dnic2des(char *dnic);
extern char *rs_get_addr(char *);
extern char *fpac2asc(rose_address *);

extern int cfg_open(cfg_t *);
extern int open_cfg(cfg_t *cfg, FILE *fptr, int);
extern int is_heard(char **);


extern void fpac_nr_config_load_ports(void);

extern void MD5String (char *dest, char *source);
extern void MD2String (char *dest, char *source);

/*
 * From FlexNode
 */

#ifndef AF_FLEXNET
#define AF_FLEXNET 128 
#endif

/*
 * /var/ax25/flex/gateways: (example)
 * addr  callsign  dev  dest  digipeaters
 * 00001 PI4TUE    ax1   935
 */

struct flex_gt {
  int                     addr;
  char                    call[10];
  char                    dev[14];
  char                    digis[AX25_MAX_DIGIS][10];
  int                     af_mode;

  struct flex_gt          *next;
};

/*
 * /var/ax25/flex/destinations: (example)
 * callsign  ssid    rtt  gateway
 * 9A0XZG    0-15   2575    00001
 * DB0AAA    0-0      63    00001
 */

struct flex_dst {
  char                    dest_call[10];
  unsigned short          ssida;
  unsigned short          sside;
  unsigned long           rtt;
  int                     addr;
  
  struct flex_dst         *next;
};

extern struct flex_gt *read_flex_gt(void);
extern void free_flex_gt(struct flex_gt *fp);
extern struct flex_dst *read_flex_dst(void);
extern void free_flex_dst(struct flex_dst *fp);
extern struct flex_dst *find_dest(char *dest_call, struct flex_dst *list);
extern struct flex_gt *find_gateway(int addr, struct flex_gt *list);

#endif /* __FPAC_H */
