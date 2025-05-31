
/******************************************************
 * node/node.h                                        *
 * FPAC project.            FPAC NODE                 *
 *                                                    *
 * Parts of code from different sources of ax25-utils *
 *                                                    *
 * F6FBB 05-1997                                      *
 *                                                    *
 ******************************************************/

/*#include <linux/ax25.h>
 *#include <linux/netrom.h>
 *#include <linux/rose.h>
 */
#include <sys/ipc.h>	/* for key_t */
#include "../lib/ax25compat.h"
#include "../lib/fpac.h"
#include "../lib/wp.h"

#define STATE_IDLE	0
#define STATE_TRYING	1
#define STATE_CONNECTED	2
#define STATE_PINGING	3
#define STATE_EXTCMD	4
#define STATE_LOGIN	5

#define LOGLVL_NONE	0
#define LOGLVL_ERROR	1
#define LOGLVL_LOGIN	2
#define LOGLVL_GW	3

#define PERM_LOGIN		1	/* Permit login			*/
#define PERM_AX25		2	/* AX.25 gatewaying		*/
#define PERM_NETROM		4	/* NETROM gatewaying		*/
#define PERM_TELNET_LOCAL	8	/* Telnet to "local" hosts	*/
#define PERM_TELNET_AMPR	16	/* Telnet to 44.xx.xx.xx hosts	*/
#define PERM_TELNET_INET	32	/* Telnet to other hosts	*/
#define PERM_HIDDEN		64	/* Use hidden ports		*/
#define PERM_ROSE		128	/* ROSE gatewaying		*/
#define PERM_NOESC		256	/* No escape character		*/

#define PERM_TELNET (PERM_TELNET_LOCAL & PERM_TELNET_AMPR & PERM_TELNET_INET)


/* Character Colors */
#define ResetColor    "\033[0m"
/* Foreground Colours */
#define F_Default     "\033[0;39m"
#define F_Red         "\033[1;91m"
#define F_Green       "\033[1;92m"
#define F_Yellow      "\033[1;93m"
#define F_Blue        "\033[1;94m"
#define F_Magenta     "\033[1;95m"
#define F_Cyan        "\033[1;96m"
#define F_White       "\033[1;97m"
#define F_Black       "\033[0;30m"
#define F_DarkRed     "\033[0;31m"
#define F_DarkGreen   "\033[0;32m"
#define F_DarkYellow  "\033[0;33m"
#define F_DarkBlue    "\033[0;34m"
#define F_DarkMagenta "\033[0;35m"
#define F_DarkCyan    "\033[0;36m"
#define F_Gray        "\033[0;37m"
#define F_DarkGray    "\033[1;90m"
/*Background Colours */
#define B_Default      "\033[49m"
#define B_Black        "\033[40m"
#define B_DarkRed      "\033[41m"
#define B_DarkGreen    "\033[42m"
#define B_DarkYellow   "\033[43m"
#define B_DarkBlue     "\033[44m"
#define B_DarkMagenta  "\033[45m"
#define B_DarkCyan     "\033[46m"
#define B_Gray         "\033[47m"
#define B_DarkGray     "\033[100m"
#define B_Red          "\033[101m"
#define B_Green        "\033[102m"
#define B_Yellow       "\033[103m"
#define B_Blue         "\033[104m"
#define B_Magenta      "\033[105m"
#define B_Cyan         "\033[106m"
#define B_White        "\033[107m"

struct cmd {
	char	*name;
	int	valid;
	int	len;
	int	type;
	int	visible;
	int	(*function) (int argc, char **argv);
	char	*command;
	int	flags;
	int	uid;
	int	gid;
	char	*path;
	struct cmd *next;
};

struct fpac_user
{
	int		pid;
	int		fd;
	key_t		ipc_key;
	long		logintime;
	long		cmdtime;
	unsigned char	state;
	char		call[10];
	unsigned short	ul_type;
	unsigned short	dl_type;
	char		ul_name[32];
	char		dl_name[32];
	char    	ul_port[32];
	char    	dl_port[32];

	char		unused[92];
};

extern struct fpac_user User;

extern long IdleTimeout;
extern long ConnTimeout;
extern int ReConnectTo;
extern int LogLevel;
extern int EscChar;

extern char HostName[40];
extern char *NodeId;
extern char *NrPort;

extern cfg_t cfg;

extern struct cmd *Nodecmds;
extern struct cmd *Syscmds;

#define	CMD_INTERNAL	1
#define	CMD_ALIAS	2
#define	CMD_EXTERNAL	3

#define min(a,b)	((a) < (b) ? (a) : (b))
#define max(a,b)	((a) > (b) ? (a) : (b))

/* in nodeio.c */
extern int tprintf(const char *, ...);

/* in nodeconf.c */
extern int read_conf(int verbose);

/* in cmdparse.c */
extern void free_cmdlist(struct cmd *list);
extern void insert_cmd(struct cmd **list, struct cmd *new);
extern int add_internal_cmd(struct cmd **list, char *name, int len, int visible, int (*function) (int argc, char **argv));
extern int parse_args(char **argv, char *cmd);
extern int cmdparse(struct cmd *cmdp, char *cmdline);

/* in util.c */
extern void node_msg(const char *fmt, ...);
extern void node_perror(char *str, int err);
extern char *print_node(const char *alias, const char *call);
extern void fpaclog(int, const char *, ...);

/* in user.c */
extern void login_user(void);
extern void logout_user(void);
extern void update_user(void);
extern int user_list(int argc, char **argv);
extern int user_count(void);

/* in config.c */
extern int is_hidden(const char *port);
extern int check_perms(int what, unsigned long peer);
extern char *read_perms(struct fpac_user *up, unsigned long peer);
extern int read_config(void);

/* in command.c */
extern int Colored;
extern char *roseaddr(char *addr);
extern void init_nodecmds(void);
extern void logout(char *reason);
extern int do_alias(int argc, char **argv);
extern int do_application(int argc, char **argv);
extern int do_bye(int argc, char **argv);
extern int do_color(int argc, char **argv);
extern int do_mheard(int argc, char **argv);
extern int do_dest(int argc, char **argv);
extern int do_help(int argc, char **argv);
extern int do_host(int argc, char **argv);
extern int do_ports(int argc, char **argv);
extern int do_links(int argc, char **argv);
extern int do_routes(int argc, char **argv);
extern int do_netrom(int argc, char **argv);
extern int do_status(int argc, char **argv);
extern int do_users(int argc, char **argv);
extern int do_dir(int argc, char **argv);
extern int do_wp(int argc, char **argv);
extern int do_yapp(int argc, char **argv);
extern int do_rose(int argc, char **argv);

/* in gateway.c */
extern char *rs_get_addr(char *dev);
extern int do_connect(int argc, char **argv);
extern int do_finger(int argc, char **argv);
extern int do_ping(int argc, char **argv);

/* in ipc.c */
extern int ipc_open(void);
extern int ipc_close(void);
extern int do_talk(int argc, char **argv);

/* in extcmd.c */
extern int extcmd(struct cmd *cmdp, char **argv);

/* in fpaccmd.c */
extern int add_alias_cmd(struct cmd **list, char *cmd, char *arg);

/* in nodesys.c */
extern int do_sysop(int argc, char **argv);
extern int is_sysop(void);
