/******************************************************
 * libcfg.c                                           *
 * FPAC project.            FPAC PAD                  *
 *                                                    *
 * Parts of code from different sources of ax25-utils *
 *                                                    *
 * F6FBB 05-1997                                      *
 *                                                    *
 ******************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/*#include <unistd.h>*/
#include <signal.h>
#include <syslog.h>
#include <ctype.h>

/*#include <sys/types.h>*/
/*#include <sys/fcntl.h>*/
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
/*#include <sys/socket.h>*/

#include <arpa/inet.h>

#include "ax25compat.h"

#include "fpac.h"

enum conf_kw
{
	CALLSIGN,
	TRCALL,
	COMMAND,
	PATH,
	CITY,
	STATE,
	COUNTRY,
	LOCATOR,
	ALTERNATE,
	DNIC,
	ADDRESS,
	COVERAGE,
	NODE,
	ROUTES,
	PORT,
	LUSER,
	USERPORT,
	DEFPORT,
	ADDPORT,
	ALIAS,
	PASSWORD,
	SYSOP,
	APPLICATION,
	INETPORT,
	OPTION,
	NOWP,
	INETADDR,
	END
};

typedef struct
{
	char keyword[20];
	int  ident;
} kw_t;

kw_t kw[] = 
{
	{ "l3call", CALLSIGN },
	{ "command", COMMAND },
	{ "path", PATH },
	{ "city", CITY },
	{ "state", STATE },
	{ "country", COUNTRY },
	{ "locator", LOCATOR },
	{ "l2call", ALTERNATE },
	{ "trcall", TRCALL },
	{ "dnic", DNIC },
	{ "address", ADDRESS },
	{ "coverage", COVERAGE },
	{ "node", NODE },
	{ "routes", ROUTES },
	{ "port", PORT },
	{ "user", LUSER },
	{ "userport", USERPORT },
	{ "defport", DEFPORT },
	{ "addport", ADDPORT },
	{ "alias", ALIAS },
	{ "password", PASSWORD },
	{ "sysop", SYSOP },
	{ "application", APPLICATION },
	{ "inetport", INETPORT },
	{ "option", OPTION },
	{ "nowp", NOWP },
	{ "inetaddr", INETADDR },
	{ "end", END },
	{ "", -1}	/* End of the table */
};

static dniclu_t *dnic_list = NULL;
static int nb_dnic = 0;
#define DNICBLOCKSIZE	100

static long des2long(char *str)
{
	long val = 0L;
	int c;
	int nb = 0;
	
	while (*str)
	{
		if (nb == 3)
			return (0L);
		c = (islower(*str)) ? toupper(*str) : *str;
		val += (c << (nb * 8));
		++str;
		++nb;
	}
	return val;
}

static char *long2des(long val)
{
	static char des[4];
	
	des[0] = val & 0xff;
	des[1] = (val >> 8) & 0xff;
	des[2] = (val >> 16) & 0xff;
	des[3] = '\0';
	
	return des;
}

char *des2dnic(char *des)
{
	int i;
	long country;
	static char dnic[5];
	
	country = des2long(des);
	if (country == 0)
		return NULL;
		
	for (i = 0 ; i < nb_dnic ; i++)
	{
		if (dnic_list[i].country == country)
		{
			sprintf(dnic, "%d", dnic_list[i].dnic);
			return dnic;
		}
	}
	return NULL;
}
	
char *dnic2des(char *dnic)
{
	int d;
	int i;
	char ldnic[5];
	
	/* Get only the first 4 digits */
	memcpy(ldnic, dnic, 4);
	ldnic[4] = '\0';
	
	d = atoi(ldnic);
	if (d < 1000 || d > 9999)
		return NULL;
		
	for (i = 0 ; i < nb_dnic ; i++)
	{
		if (dnic_list[i].dnic == d)
		{
			return long2des(dnic_list[i].country);
		}
	}
	return NULL;
}

/* Open the fpac.dnic file and read it in to memory. */	
static void dnic_open(void)
{
	int max = 0;
	FILE *fptr;
	char line[256];
	char designator[256];
	int dnic;
	
	/* Abort if we can't open the fpac.dnic file */
	if ((fptr = fopen(FPACDNIC, "r")) == NULL)
		return;
	
	/* Read the file line by line */	
	while (fgets(line, sizeof(line), fptr))
	{
		if (nb_dnic == max)
		{
			max += DNICBLOCKSIZE;
			if (dnic_list)
				dnic_list = realloc(dnic_list, max);
			else
				dnic_list = malloc(sizeof(dniclu_t) * max);
		}

		/* Split the line from the file in to country designator and DNIC and store them */
		if ((sscanf(line, "%s %d", designator, &dnic) >= 2) && dnic > 0 && dnic < 10000)
		{
			if (*designator == '#') /* Ignore commented lines */
				continue;
			
			dnic_list[nb_dnic].country = des2long(designator);
			dnic_list[nb_dnic].dnic = dnic;
			++nb_dnic;
		}
	}
	
	fclose(fptr);
}

/* Return a keyword identifier or -1 if not found */
static int kwid(char *keyword)
{
	int i;

	for (i = 0 ; kw[i].keyword[0] ; i++)
	{
		if (strcasecmp(keyword, kw[i].keyword) ==0)
			break;
	}
	return(kw[i].ident);
}

/* Cleans the beginning and the end of a string */
static char *strclean(char *str)
{
	int len;

	while (isspace(*str))
		++str;

	len = strlen(str);

	while ((len > 0) && (!isgraph(str[len-1])))
	{
		str[len-1] = '\0';
		--len;
	}

	return(str);
}

/* Take a line from a config file and check it for a keyword/value pair */
/* Returns the keyword and the value */
static int get_value(char *line, char *keyword, char *value)
{
	char *ptr;

	*keyword = *value = '\0';

	line = strclean(line); /* Trim the line */
	if ((*line == '\0') || (*line == '#')) /* Ignore null lines or comments */
		return(0);

	ptr = strchr(line, '=');
	if (ptr)
	{
		*ptr++ = '\0';
		strcpy(value, strclean(ptr));
	}

	strcpy(keyword, strclean(line));

	return(1);	
}

static void strcpy_n(char *rec, char *src, int len)
{
	memset(rec, '\0', len);
	strncpy(rec, src, len-1);
}

/* Add a new application 
format is add_application(config, keyword, value)
*/
static void add_application(cfg_t *cfg, char *call, char *appli)
{
	appli_t *a;
	
	/* If there is no SSID appended to the application's callsign
	then we append a -0 on the end of the call */
	if (!strchr(call, '-'))
		strcat(call, "-0");

	a = calloc(1, sizeof(appli_t));
	strcpy_n(a->call, call, sizeof(a->call));
	strcpy_n(a->appli, appli, sizeof(a->appli));
	a->next = cfg->appli;
	cfg->appli = a;
/* FET */
	syslog(LOG_INFO, "Adding application %s as call %s\n", a->appli, a->call);
}

int cfg_open(cfg_t *cfg)
{
	int errnum;
	char line[256];
	FILE *fptr;
	struct stat st;

	/* Find mandatory FPAC configuration file*/
	fptr = fopen(FPACCONF, "r");
	if (fptr == NULL)
	{
		fprintf(stdout, "Cannot open file %s\n", FPACCONF);
		syslog(LOG_ERR, "Cannot open file %s\n", FPACCONF);
		return(1);
	}

	memset(cfg, 0, sizeof(cfg_t));

	/* Default IP address for rose ports */
	strcpy(cfg->def_addr, "192.168.0.1");

	/* Open the fpac.dnic file and store it in memory */
	dnic_open();

	/* Get the fpac.conf last modified time/date */
	if (stat(FPACCONF, &st) == 0)
	{
		cfg->date = st.st_mtime;
	}
	
	/* Application FPACNODE always exists, so add it. This makes the
	node respond to the call NODE- with any SSID */
	add_application(cfg, "NODE-*", FPACNODE);

	/* Read mandatory FPAC configuration file*/
	open_cfg(cfg, fptr, 1);
	fclose(fptr);

	/* Read optional FPAC NODE list file if it exists*/
	fptr = fopen(FPACNODES, "r");
	if (fptr != NULL) {
		open_cfg(cfg, fptr, 0);
		fclose(fptr);
	}

	/* Get the fpac.nodes last modified time/date */
	if (stat(FPACNODES, &st) == 0)
	{
		if(st.st_mtime > cfg->date)
			cfg->date = st.st_mtime;
	}
	
	
	/* Read optional FPAC ROUTES file if it exists*/
	fptr = fopen(FPACROUTES, "r");
	if (fptr != NULL) {
		open_cfg(cfg, fptr, 0);
		fclose(fptr);
	}

	/* Get the fpac.routes last modified time/date */
	if (stat(FPACROUTES, &st) == 0)
	{
		if(st.st_mtime > cfg->date)
			cfg->date = st.st_mtime;
	}
	
	
	/* Check for mandatory fields */
	if (*cfg->callsign == '\0')		errnum = CALLSIGN;
	else if (*cfg->alt_callsign == '\0')	errnum = ALTERNATE;
	else if (*cfg->city == '\0')		errnum = CITY;
	else if (*cfg->locator == '\0')		errnum = LOCATOR;
	else if (*cfg->dnic == '\0')		errnum = DNIC;
	else if (*cfg->address == '\0')		errnum = ADDRESS;
	else errnum = -1;
	
	if (errnum != -1) {
		fprintf(stderr, "field \"%s\" not initalized in %s\n", kw[errnum].keyword, FPACCONF);
		syslog(LOG_ERR, "field \"%s\" not initalized in %s\n", kw[errnum].keyword, FPACCONF);
		return(2);
	}

	/* Build local full address */
	strcpy(cfg->fulladdr, cfg->dnic);
	strcat(cfg->fulladdr, cfg->address);

	/* Set some environment variables */
	sprintf(line, "FPAC_L2CALL=%s",  cfg->alt_callsign);
	putenv(line);
	sprintf(line, "FPAC_L3CALL=%s",  cfg->callsign);
	putenv(line);
	sprintf(line, "FPAC_ADDRESS=%s", cfg->fulladdr);
	putenv(line);
	sprintf(line, "FPAC_CITY=%s",    cfg->city);
	putenv(line);
	sprintf(line, "FPAC_LOCATOR=%s", cfg->locator);
	putenv(line);

	return(0);
}

/* Read the referenced config file, parse out the values, and
populate the necessary arrays with the keywords/values */
int open_cfg(cfg_t *cfg, FILE *fptr, int first)
{
	int mode = 0;
	char *stmp;
	char keyword[80];
	char value[256];
	char full_addr[20];
	char dnic_cur[5];
	char line[256];
	char ax25_port[256];
	char *ptr;
	addrp_t	*d;
	alias_t *a;
	cmd_t	*c;
	luser_t	*l;
	node_t	*n;
	route_t *r;
	port_t	*p;
	cover_t	*o;

	/* first is set when we read the fpac.conf file only */
	if (first) {
		d = NULL;
		a = NULL;
		c = NULL;
		l = NULL;
		n = NULL;
		r = NULL;
		p = NULL;
		o = NULL;
	}

	/* Read the file line by line until we run out of lines to read */
	while (fgets(line, sizeof(line), fptr))
	{

		/* Figure out if the line we read has a keyword/value pair */
		if (!get_value(line, keyword, value))
			continue;

		/* This switch is run AFTER mode has been set (if applicable)
		by the default case down below. mode is reset after the block
		(case) is processed, and then we move on to find the next block
		to process. */
		switch (mode)
		{
		/* Check the command block and add/overwrite any new commands we find */
		case COMMAND :
			switch(kwid(keyword))
			{
			case END:
				mode = 0;
				break;
			default:
				/* New command */
				c = calloc(1, sizeof(cmd_t));
				if (c == NULL)
					return(1);
				strcpy_n(c->name, keyword, sizeof(c->name));
				c->cmd = malloc(strlen(value)+1);
				if (c->cmd == NULL)
					return(1);
				strcpy(c->cmd, value);
				c->next = cfg->cmd;
				cfg->cmd = c;
				break;
			}
			break;
		/* Check the sysop block and add/overwrite any new commands we find */
		case SYSOP :
			switch(kwid(keyword))
			{
			case END:
				mode = 0;
				break;
			default:
				/* New command */
				c = calloc(1, sizeof(cmd_t));
				if (c == NULL)
					return(1);
				strcpy_n(c->name, keyword, sizeof(c->name));
				c->cmd = malloc(strlen(value)+1);
				if (c->cmd == NULL)
					return(1);
				strcpy(c->cmd, value);
				c->next = cfg->syscmd;
				cfg->syscmd = c;
				break;
			}
			break;
		/* Read in the node blocks (either from fpac.conf or fpac.nodes */
		case NODE:
			switch(kwid(keyword))
			{
			case PATH:
				strcpy_n(n->call, value, sizeof(n->call));
				break;
			case DNIC:
				strcpy_n(n->dnic, value, sizeof(n->dnic));
				break;
			case ADDRESS:
				strcpy_n(n->addr, value, sizeof(n->addr));
				break;
			case PORT:
				strcpy_n(n->port, value, sizeof(n->port));
				break;
			case NOWP:
				n->nowp = (*value != '0');
				break;
			case END:
				n->next = cfg->node;
				cfg->node = n;
				mode = 0;
				break;
			}
			break;
		/* Read in the addport block */
		case ADDPORT:
			switch(kwid(keyword))
			{
			case ADDRESS:
				strcpy_n(d->addr, value, sizeof(d->addr));
				break;
			case PORT:
				strcpy_n(d->port, value, sizeof(d->port));
				break;
			case END:
				d->next = cfg->addrp;
				cfg->addrp = d;
				mode = 0;
				break;
			}
			break;
		/* Read in the application block and add any applications we find */
		case APPLICATION:
			switch(kwid(keyword))
			{
			case END:
				mode = 0;
				break;
			default:
				add_application(cfg, keyword, value);
				break;
			}
			break;
		/* Read in the user block and add any routes for specific users */
		case LUSER:
			switch(kwid(keyword))
			{
			case PATH:
				strcpy_n(l->call, value, sizeof(l->call));
				break;
			case PORT:
				strcpy_n(l->port, value, sizeof(l->port));
				break;
			case END:
				l->next = cfg->luser;
				cfg->luser = l;
				mode = 0;
				break;
			}
			break;
		/* Read in the routes block either from fpac.conf or fpac.routes */
		case ROUTES:
			switch(kwid(keyword))
			{
			case DNIC:
				strcpy_n(dnic_cur, value, sizeof(dnic_cur));
				break;
			case END:
				mode = 0;
				break;
			default :
				r = calloc(1, sizeof(route_t));
				full_addr[0] = '\0';
				if (strcmp(dnic_cur, "0") == 0)
				{
			/* DNIC = 0 means that addresses will have up to 10 digits */
					keyword[10] = '\0';
				}
				else
				{
			/* if DNIC is declared addresses will have up to 6 digits */
					strcpy(full_addr, dnic_cur);
					keyword[6] = '\0';
				}
				if (*keyword != '*')
					strcat(full_addr, keyword);
				strcpy_n(r->addr, full_addr, sizeof(r->addr));
				strcpy_n(r->nodes, value, sizeof(r->nodes));
				r->next = cfg->route;
				cfg->route = r;
				break;
			}
			break;
		/* Read in the alias block and add the routes */
		case ALIAS:
			switch(kwid(keyword))
			{
			case PATH:
				ptr = value;
				while (*ptr)
				{
					if (*ptr == ',')
						*ptr = ' ';
					++ptr;
				}
				strcpy_n(a->path, value, sizeof(a->path));
				break;
			case END:
				a->next = cfg->alias;
				cfg->alias = a;
				mode = 0;
				break;
			}
			break;
		default:
			/* This switch looks for the defined keywords in the config file
			and if the block needs further processing, it sets the mode so
			the above switch will run and process further. */
			switch(kwid(keyword))
			{
			case OPTION:
				strcpy_n(cfg->option, value, sizeof(cfg->option));
				break;
			case PASSWORD:
				strcpy_n(cfg->password, value, sizeof(cfg->password));
				break;
			case CALLSIGN: /* this is the L3call of the node */
				strcpy_n(cfg->callsign, value, sizeof(cfg->callsign));
				break;
			case TRCALL:
				strcpy_n(cfg->trt_callsign, value, sizeof(cfg->trt_callsign));
				break;
			case ALTERNATE: /* this is the L2call of the node */
				strcpy_n(cfg->alt_callsign, value, sizeof(cfg->alt_callsign));
			/* Application alias callsign always exist */
				add_application(cfg, value, FPACNODE);
				break;
			case CITY:
				strcpy_n(cfg->city, value, sizeof(cfg->city));
				break;
			case STATE:
				strcpy_n(cfg->state, value, sizeof(cfg->state));
				break;
			case COUNTRY:
				strcpy_n(cfg->country, value, sizeof(cfg->country));
				break;
			case LOCATOR:
				strcpy_n(cfg->locator, value, sizeof(cfg->locator));
				break;
			case DNIC:
				strcpy_n(cfg->dnic, value, sizeof(cfg->dnic));
				break;
			case ADDRESS:
				strcpy_n(cfg->address, value, sizeof(cfg->address));
				break;
			case COVERAGE:
				ptr = strtok(value, " ,\t");
				while (ptr)
				{
					o = calloc(1, sizeof(cover_t));
					strcpy_n(o->addr, ptr, sizeof(o->addr));
					o->next = cfg->cover;
					cfg->cover = o;
					ptr = strtok(NULL, " ,\t");
				}
				break;
			case DEFPORT:
				strcpy_n(cfg->def_port, value, sizeof(cfg->def_port));
				break;
			case USERPORT:
				strcpy_n(ax25_port, value, sizeof(ax25_port));
				break;
			case INETPORT:
				cfg->inetport = atoi(value);
				break;
			case INETADDR:
				strcpy_n(cfg->def_addr, value, sizeof(cfg->def_addr));
				break;
			case NODE:
				n = calloc(1, sizeof(node_t));
				strcpy_n(n->name, value, sizeof(n->name));
				mode = NODE;
				break;
			case LUSER:
				l = calloc(1, sizeof(luser_t));
				strcpy_n(l->name, value, sizeof(l->name));
				mode = LUSER;
				break;
			case ADDPORT:
				d = calloc(1, sizeof(addrp_t));
				d->fd = -1;
				strcpy_n(d->name, value, sizeof(d->name));
				mode = ADDPORT;
				break;
			case ALIAS:
				a = calloc(1, sizeof(alias_t));
				a->fd = -1;
				strcpy_n(a->alias, value, 10);
				mode = ALIAS;
				break;
			case COMMAND:
				mode = COMMAND;
				break;
			case APPLICATION:
				mode = APPLICATION;
				break;
			case SYSOP:
				mode = SYSOP;
				break;
			case ROUTES:
				strcpy(dnic_cur, cfg->dnic);
				mode = ROUTES;
				break;
			}
			break;
		}

	}

	if (first) /* first is set if this is fpac.conf we're working on */
	{
	/* Create port list */

	stmp = strdup(ax25_port);
	ptr = strtok(stmp, " ,\t");
	while (ptr)
	{
		appli_t *ap;

		p = malloc(sizeof(port_t));
		p->fd_call = -1;
		p->fd_digi = -1;
		p->fd_appli = -1;
		p->alias  = NULL;
		p->applis = NULL;
		strcpy(p->name, ptr);
		for (a = cfg->alias ; (a != NULL) ; a = a->next)
		{
			alias_t *alias;

			alias = malloc(sizeof(alias_t));
			memcpy(alias, a, sizeof(alias_t));
			alias->next = p->alias;
			p->alias = alias;
		}
		for (ap = cfg->appli ; (ap != NULL) ; ap = ap->next)
		{
			appli_t *appli;
			if (strcmp(ap->appli, FPACNODE) != 0)
			{
				appli = malloc(sizeof(appli_t));
				memcpy(appli, ap, sizeof(appli_t));
				appli->next = p->applis;
				p->applis = appli;
			}
		}
		p->next = cfg->port;
		cfg->port = p;
		ptr = strtok(NULL, " ,\t");
	}
	free(stmp);
	}
	return(0);

}
