/******************************************************
 * fpacnode.c                                         *
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
#include <unistd.h>
#include <errno.h>
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

#include <arpa/inet.h>

/* Variables are not extern in main */
#define extern

#include "config.h"
#include "ax25compat.h"
#include "wp.h"
#include "node.h"
#include "io.h"


int LogLevel = LOGLVL_ERROR;
long IdleTimeout = 900L;
char *NodeId = NULL;
cfg_t cfg;

static void alarm_handler(int sig)
{
	set_eolmode(User.fd, EOLMODE_TEXT);
	tputs("\n");
	node_msg("Timeout! Disconnecting...");
	logout("Timeout");
}

static void term_handler(int sig)
{
	set_eolmode(User.fd, EOLMODE_TEXT);
	tputs("\n");
	node_msg("System going down! Disconnecting...");
	logout("SIGTERM");
}

static void quit_handler(int sig)
{
	set_eolmode(User.fd, EOLMODE_TEXT);
	tputs("\n");
	node_msg("User terminated at remote");
	logout("SIGQUIT");
}

static void prompt(void)
{
	tprintf("%s (Commands = ?) : ", cfg.alt_callsign);
}

int check_rose(void)
{
	int n1,n2,n3;
	int loopback = -1;
	struct proc_rs_route *tp, *tlist;
	struct proc_rs_nodes *rsno, *rsnolist;
	struct proc_rs_neigh *rsne, *rsnelist;

	n1=n2=n3=0;

		tlist = read_proc_rs_routes();
/*		if ((tlist = read_proc_rs_routes()) == NULL && errno != 0)
		printf("check_rose: read_proc_rs_routes %d\n", errno);*/

		for (tp = tlist; tp != NULL; tp = tp->next)
		{
			++n1;
		}
/*		printf("\nFPAC L3 Transits : %d\n", n1);*/
		free_proc_rs_routes(tlist);

		rsnelist = read_proc_rs_neigh();
/*		if ((rsnelist = read_proc_rs_neigh()) == NULL && errno != 0)
			printf("check_rose: read_proc_rs_neigh %d\n", errno);*/

		for (rsne = rsnelist; rsne != NULL; rsne = rsne->next)
		{
			if (strncmp(rsne->call, "RSLOOP", 6) == 0)
			{
				loopback = rsne->addr;
				continue;
			}
			++n2;
		}
/*		printf("FPAC adjacents   : %d\n", n2);*/
		free_proc_rs_neigh(rsnelist);
	
		rsnolist = read_proc_rs_nodes();
/*		if ((rsnolist = read_proc_rs_nodes()) == NULL && errno != 0)
			printf("check_rose: read_proc_rs_nodes %d\n", errno);*/

		for (rsno = rsnolist; rsno != NULL; rsno = rsno->next)
		{
			if (rsno->neigh1 == loopback) 
				continue;
			++n3;
		}
	/*	printf("FPAC Routes      : %d\n", n3);*/
		free_proc_rs_nodes(rsnolist);
	
	return n1+n2+n3;
}


int main(int argc, char **argv)
{
	int yes;
	char NodeName[80];
	cmd_t *c;
	union {
		struct full_sockaddr_ax25 sax;
		struct full_sockaddr_rose      srose;
		struct sockaddr_in        sin;
	} saddr;
	unsigned int slen = sizeof(saddr);
	char *p;
	char *ptr;
	int paclen;
	int invalid_cmds = 0;
	FILE *fp;
	char line[256];
/*	char str[80];
	char *ps;
*/
	struct proc_rs *rlist;

	if ((rlist = read_proc_rs ()) == NULL && errno != 0) {
		printf("\nNo proc/rose. Rose module is probably not loaded - start FPAC first !\n\n");
		free_proc_rs(rlist);
		return 1;
	}
		free_proc_rs(rlist);
	
	/* Check if there are any Rose Routes or Nodes */
	if (check_rose() <= 3 )
	{
		printf("\nWARNING !     Less than 3 Rose Routes or Nodes defined\n");
		printf("\n              Check routes configuration and that FPAD is running !\n\n");

/*		return 1;*/
	}

	memset(&saddr.srose, 0x00, sizeof(struct full_sockaddr_rose));

	signal(SIGALRM, alarm_handler);
	signal(SIGTERM, term_handler);
	signal(SIGPIPE, quit_handler);
	signal(SIGQUIT, quit_handler); 

	NodeId = NodeName;

	if (cfg_open(&cfg) != 0)
	{
		return 1;
	}

	if (ax25_config_load_ports() == 0) 
	{
		fpaclog(LOGLVL_ERROR, "No AX.25 port data configured");
		return 1;
	}

	fpac_nr_config_load_ports();

	if (argc  > 1) { 
		strncpy(User.call, strupr(argv[1]), 9);
		User.call[9] = 0;
		if (strstr(User.call, "-") == NULL)
			strcat(User.call, "-0");
	}
	
	/*
	rs_config_load_ports();
	*/

	/* Add commands and sysop commands */
	for (c = cfg.cmd ; c ; c = c->next)
		add_alias_cmd(&Nodecmds, c->name, c->cmd);

	for (c = cfg.syscmd ; c ; c = c->next)
		add_alias_cmd(&Syscmds, c->name, c->cmd);


	if (getpeername(STDOUT_FILENO, (struct sockaddr *)&saddr, &slen) == -1)
	{
		if (errno != ENOTSOCK)
		{
			fpaclog(LOGLVL_ERROR, "getpeername: %s", strerror(errno));
			return 1;
		}
		User.ul_type = AF_UNSPEC;
	} 
	else
		User.ul_type = saddr.sax.fsa_ax25.sax25_family;

	switch (User.ul_type) 
	{
	case AF_AX25:
		strcpy(User.call, ax25_ntoa(&saddr.sax.fsa_ax25.sax25_call));
		if (getsockname(STDOUT_FILENO, (struct sockaddr *)&saddr.sax, &slen) == -1) {
			fpaclog(LOGLVL_ERROR, "getsockname: %s", strerror(errno));
			return 1;
		}
		strcpy(User.ul_name, ax25_config_get_port(&saddr.sax.fsa_digipeater[0]));
		paclen = ax25_config_get_paclen(User.ul_name);
		p = AX25_EOL;
		break;
	case AF_NETROM:
		strcpy(User.call, ax25_ntoa(&saddr.sax.fsa_ax25.sax25_call));
		strcpy(User.ul_name, ax25_ntoa(&saddr.sax.fsa_digipeater[0]));
		if (getsockname(STDOUT_FILENO, (struct sockaddr *)&saddr.sax, &slen) == -1) {
			fpaclog(LOGLVL_ERROR, "getsockname: %s", strerror(errno));
			return 1;
		}
		strcpy(User.ul_port, nr_config_get_port(&saddr.sax.fsa_ax25.sax25_call));
		paclen = nr_config_get_paclen(User.ul_port);
		p = NETROM_EOL;
		break;
	case AF_ROSE:
		/* Accept the connection */
		yes = 1;
		ioctl(STDOUT_FILENO, SIOCRSACCEPT, &yes);
		strcpy(User.call, ax25_ntoa(&saddr.srose.srose_call));
		strcpy(User.ul_name, fpac2asc(&saddr.srose.srose_addr));
		/* paclen = rs_config_get_paclen(NULL); */
		paclen = ROSE_MTU;			/* 251 */
		p = ROSE_EOL;
		break;
	case AF_INET:
		ptr = getenv("CALL_TCP");
		if (ptr)
		{
			strcpy(User.call, ptr);
			paclen = ROSE_MTU;		/* 251 */
			p = AX25_EOL;
		}
		else
		{
			paclen = 1024;
			p = INET_EOL;
		}
		strcpy(User.ul_name, inet_ntoa(saddr.sin.sin_addr));
		break;
	case AF_UNSPEC:
		paclen = 1024;
		p = INET_EOL;
		break;
	default:
		fpaclog(LOGLVL_ERROR, "Unsupported address family %d", User.ul_type);
		return 1;
	}

	if (init_io(User.fd, paclen, p) == -1) 
	{
		write(User.fd, "Error initializing I/O.\r\n", 25);
		fpaclog(LOGLVL_ERROR, "Error initializing I/O");
		return 1;
	}
	
	if ((User.ul_type == AF_INET) && (!strcmp(p, INET_EOL)))
	{
		set_telnetmode(User.fd, 1);
		tn_do_linemode(User.fd);
	}

	init_nodecmds();

	User.state = STATE_LOGIN;
	login_user();

	gethostname(HostName, sizeof(HostName));

	if (User.call[0] == 0) 
	{
		char str[80];

		tprintf("\nFPAC-Node v %s (built %s) %s\n\ncallsign: ", VERSION, __DATE__, HostName);
		usflush(User.fd);
		alarm(300L);			/* 5 min timeout */
		if ((p = readline(User.fd)) == NULL)
			logout("User disconnected");
		alarm(0L);
		strncpy(User.call, p, 9);
		User.call[9] = 0;
		strlwr(User.call);

		sprintf(str, "CALL_TCP=%s", User.call);
		putenv(str);
	}

	sprintf(NodeId,"%s", cfg.alt_callsign);

/*	if ((p = strstr(User.call, "-0")) != NULL)
		*p = 0; */
	if (strstr(User.call, "-") == NULL)
		strcat(User.call, "-0");
	strupr(User.call);

	if (wp_check_call(User.call) == -1) 
	{
		node_msg("Invalid callsign");
		fpaclog(LOGLVL_LOGIN, "Invalid callsign %s @ %s", User.call, User.ul_name);
		logout("Invalid callsign");
	}
		
	node_msg ("User call : %s", User.call);

	if ((fp = fopen (FPAC_HELLO_FILE, "r")) != NULL) 
	{
		while (fgets (line, 256, fp) != NULL)
			tputs (line);
		tputs ("\n");
		fclose (fp);
	}

	node_msg("%s v %s (built %s) for LINUX (help = h)", "FPAC-Node", VERSION, __DATE__);

	for (;;)
	{
		char *ps;

		prompt();

		usflush(User.fd);
		User.state = STATE_IDLE;
		time(&User.cmdtime);
		update_user();
		alarm(IdleTimeout);
		if ((p = readline(User.fd)) == NULL) 
		{
			if (errno == EINTR)
				continue;
			logout("User disconnected");
		}
		alarm(IdleTimeout);
		time(&User.cmdtime);
		update_user();
		ps = strdup(p);
		if ((!is_sysop()) || (cmdparse(Syscmds, ps) == -1))
		{
			if (cmdparse(Nodecmds, p) == -1) 
			{
				if (++invalid_cmds < 3) 
				{
					node_msg("Unknown command. Type ? for a list");
				} 
				else 
				{
					node_msg("Too many invalid commands. Disconnecting..."); 
					logout("Too many invalid commands");
				}
			}
			else
				invalid_cmds = 0;
		} 
		else
			invalid_cmds = 0;

		free(ps);

		tputs("\n");
	}
	return 0;
}
