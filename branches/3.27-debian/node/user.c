/*
 * node/user.c
 *
 * FPAC project
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/file.h>
/*#include <sys/socket.h>*/
#include <signal.h>

#include "node.h"
#include "io.h"
#include "ax25compat.h"

struct user User	= {0};
static long CallerPos	= -1L;

void login_user(void)
{
	FILE *f;
	struct user u;
	long pos = 0L;
	long free = -1L;
	struct stat statbuf;

	if (stat(DATA_NODE_LOGIN_FILE, &statbuf) == -1) {
		node_perror(DATA_NODE_LOGIN_FILE, errno);
		return;
	}
	if (statbuf.st_size % sizeof(struct user) != 0) {
		node_msg("%s: Incorrect size", DATA_NODE_LOGIN_FILE);
		fpaclog(LOGLVL_ERROR, "%s: Incorrect size", DATA_NODE_LOGIN_FILE);
		return;
	}
	time(&User.logintime);
	User.cmdtime = User.logintime;
	User.pid     = getpid();
	User.fd      = STDIN_FILENO;
	if ((f = fopen(DATA_NODE_LOGIN_FILE, "r+")) == NULL) {
		node_perror(DATA_NODE_LOGIN_FILE, errno);
		return;
	}
	if (flock(fileno(f), LOCK_EX) == -1) {
		node_perror("login_user: flock", errno);
		fclose(f);
		return;
	}
	while (fread(&u, sizeof(u), 1, f) == 1) {
		if (u.pid == -1 || (kill(u.pid, 0) == -1 && errno == ESRCH)) {
			free = pos;
			break;
		}
		pos += sizeof(u);
	}
	if (free != -1L && fseek(f, free, 0L) == -1) {
		node_perror("login_user: fseek", errno);
		fclose(f);
		return;
	}
	fflush(f);
	CallerPos = ftell(f);	
	fwrite(&User, sizeof(User), 1, f);
	fflush(f);
	flock(fileno(f), LOCK_UN);
	fclose(f);
}

void logout_user(void)
{
	FILE *f;

	if (CallerPos == -1L)
		return;
	if ((f = fopen(DATA_NODE_LOGIN_FILE, "r+")) == NULL) {
		node_perror(DATA_NODE_LOGIN_FILE, errno);
		return;
	}
	if (fseek(f, CallerPos, 0) == -1) {
		node_perror("logout_user: fseek", errno);
		fclose(f);
		return;
	}
	User.pid = -1;
	fwrite(&User, sizeof(User), 1, f);
	fclose(f);
}

void update_user(void)
{
	FILE *f;

	if (CallerPos == -1L)
		return;
	if ((f = fopen(DATA_NODE_LOGIN_FILE, "r+")) == NULL) {
		node_perror(DATA_NODE_LOGIN_FILE, errno);
		return;
	}
	if (fseek(f, CallerPos, 0) == -1) {
		node_perror("update_user: fseek", errno);
		fclose(f);
		return;
	}
	fwrite(&User, sizeof(User), 1, f);
	fclose(f);
}

int user_list(int argc, char **argv)
{
	FILE *f;
	struct user u;
	struct tm *tp;
	struct proc_nr_nodes *np;
	char buf[80];
	long l;

	if ((f = fopen(DATA_NODE_LOGIN_FILE, "r")) == NULL) {
		node_perror(DATA_NODE_LOGIN_FILE, errno);
		return 0;
	}
	node_msg("Users:");
	while (fread(&u, sizeof(u), 1, f) == 1) {
		if (u.pid == -1 || (kill(u.pid, 0) == -1 && errno == ESRCH))
			continue;
		switch (u.ul_type) {
		case AF_AX25:
			sprintf(buf, "Uplink  (%.9s on port %.10s)",
				u.call, u.ul_name);
			break;
		case AF_NETROM:
			if ((np = find_node(u.ul_name, NULL)) != NULL) {
				sprintf(buf, "Circuit (%.9s %.18s)",
					u.call,
					print_node(np->alias, np->call));
			} else {
				sprintf(buf, "Circuit (%.9s %.18s)",
					u.call, u.ul_name);
			}
			break;
		case AF_ROSE:
			sprintf(buf, "ROSE    (%.9s %.18s)",
				u.call, u.ul_name);
			break;
		case AF_INET:
			sprintf(buf, "Telnet  (%.9s @ %.16s)",
				u.call, u.ul_name);
			break;
		case AF_UNSPEC:
			sprintf(buf, "Host    (%.9s on local)",
				u.call);
			break;
		default:
			/* something strange... */
			sprintf(buf, "??????  (%.9s %.18s)",
				u.call, u.ul_name);
			break;
		}
		tprintf("%-37.37s ", buf);
		switch (u.state) {
		case STATE_LOGIN:
			tputs("  -> Logging in");
			break;
		case STATE_IDLE:
			time(&l);
			l -= u.cmdtime;
			tp = gmtime(&l);
			tprintf("  -> Idle     (%d:%02d:%02d:%02d)",
				tp->tm_yday, tp->tm_hour,
				tp->tm_min, tp->tm_sec);
			break;
		case STATE_TRYING:
			switch (u.dl_type) {
			case AF_AX25:
				tprintf("  -> Trying   (%s on port %s)",
					u.dl_name, u.dl_port);
				break;
			case AF_NETROM:
				tprintf("  -> Trying   (%s)",
					u.dl_name);
				break;
			case AF_ROSE:
				tprintf("  -> Trying   (%s)",
					u.dl_name);
				break;
			case AF_INET:
				tprintf("  -> Trying   (%s:%s)",
					u.dl_name, u.dl_port);
				break;
			default:
				tputs("  -> ???");
				break;
			}
			break;
		case STATE_CONNECTED:
			switch (u.dl_type) {
			case AF_AX25:
				tprintf("<--> Downlink (%s on port %s)",
					u.dl_name, u.dl_port);
				break;
			case AF_NETROM:
				tprintf("<--> Circuit  (%s)",
					u.dl_name);
				break;
			case AF_ROSE:
				tprintf("<--> ROSE  (%s)",
					u.dl_name);
				break;
			case AF_INET:
				tprintf("<--> Telnet   (%s:%s)",
					u.dl_name, u.dl_port);
				break;
			default:
				tprintf("<--> ???");
				break;
			}
			break;
		case STATE_PINGING:
			tprintf("<--> Pinging  (%s)", u.dl_name);
			break;
		case STATE_EXTCMD:
			tprintf("<--> Extcmd   (%s)", u.dl_name);
			break;
		default:
			/* something strange... */
			tputs("  -> ??????");
			break;
		}
		tputs("\n");
	}
	fclose(f);
	return 0;
}

int user_count(void)
{
	FILE *f;
	struct user u;
	int cnt = 0;

	if ((f = fopen(DATA_NODE_LOGIN_FILE, "r")) == NULL) {
		node_perror(DATA_NODE_LOGIN_FILE, errno);
		return 0;
	}
	while (fread(&u, sizeof(u), 1, f) == 1)
		if (u.pid != -1 && (kill(u.pid, 0) != -1 || errno != ESRCH))
			cnt++;
	fclose(f);
	return cnt;
}
