/******************************************************
 * fpaccfg.c                                          *
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
/*#include <errno.h>*/
#include <signal.h>
#include <syslog.h>
#include <ctype.h>
/*#include <sys/types.h>*/
/*#include <sys/fcntl.h>*/
#include <sys/file.h>
#include <sys/ioctl.h>
/*#include <sys/socket.h>*/
#include <arpa/inet.h>

#include "ax25compat.h"
#include "fpac.h"

int main(int ac, char **av)
{

	cfg_t	cfg;
	cmd_t	*c = NULL;
	node_t	*n = NULL;
	luser_t	*l = NULL;
	alias_t	*a = NULL;
	route_t	*r = NULL;
	port_t	*p = NULL;
	appli_t	*ap = NULL;
	addrp_t	*d = NULL;
	cover_t	*o = NULL;

	if (cfg_open(&cfg) != 0)
	{
		printf("Could not read configuration file\n");
		return(1);
	}

	printf("\n");
	printf("Last change: %s", ctime(&cfg.date));

	printf("\n");
	printf("Callsign   : %s\n", cfg.callsign);
	printf("Alternate  : %s\n", cfg.alt_callsign);
	printf("DNIC       : %s\n", cfg.dnic);
	printf("Address    : %s\n", cfg.address);
	printf("Option     : %s\n", cfg.option);
	if (cfg.cover)
	{
		printf("\n");
		printf("Coverage   : ");
		for (o = cfg.cover ; o != NULL ; o = o->next)
		{
			printf("%s ", o->addr);
		}
	}

	if (cfg.inetport != 0)
	{
		printf("\n");
		printf("TCP/IP port: %d\n", cfg.inetport);
	}

	if (cfg.port)
	{
		printf("\n");
		printf("UserPort   : ");
		for (p = cfg.port ; p != NULL ; p = p->next)
		{
			printf("%s ", p->name);
		}
	}

	printf("\n");
	printf("Def. port  : %s\n", cfg.def_port);

	for (d = cfg.addrp ; d != NULL ; d = d->next)
	{
		printf("\n");
		printf("AddPort    : %s\n", d->name);
		printf("  Adress   : %s\n", d->addr);
		printf("  Port     : %s\n", d->port);
	}

	for (n = cfg.node ; n != NULL ; n = n->next)
	{
		printf("\n");
		printf("Node       : %s\n", n->name);
		printf("  Callsign : %s\n", n->call);
		printf("  DNIC     : %s\n", n->dnic);
		printf("  Adress   : %s\n", n->addr);
		printf("  Port     : %s\n", n->port);
		printf("  NoWp     : %d\n", n->nowp);
	}

	for (l = cfg.luser ; l != NULL ; l = l->next)
	{
		printf("\n");
		printf("User       : %s\n", l->name);
		printf("  Callsign : %s\n", l->call);
		printf("  Port     : %s\n", l->port);
	}

	if (cfg.alias)
	{
		printf("\n");
		for (a = cfg.alias ; a != NULL ; a = a->next)
		{
			printf("Alias      : %-10s -> %s\n", a->alias, a->path);
		}
	}

	printf("\nRoutes :\n");
	for (r = cfg.route ; r != NULL ; r = r->next)
	{
		printf("%-10s : %s\n", r->addr, r->nodes);
	}

	printf("\nApplications :\n");
	for (ap = cfg.appli ; ap != NULL ; ap = ap->next)
	{
		printf("%-10s : %s\n", ap->call, ap->appli);
	}

	printf("\nStandard commands :\n");
	for (c = cfg.cmd ; c != NULL ; c = c->next)
	{
		printf("%-10s : %s\n", c->name, c->cmd);
	}

	printf("\nSysop commands :\n");
	for (c = cfg.syscmd ; c != NULL ; c = c->next)
	{
		printf("%-10s : %s\n", c->name, c->cmd);
	}

	printf("\n");

	return(0);
}

