/*
 * node/util.c
 *
 * FPAC project
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <time.h>
#include <sys/file.h>
/*#include <sys/socket.h>*/

#include "io.h"
#include "node.h"

static char buf[256];

void node_msg(const char *fmt, ...)
{
	va_list args;
	
	va_start(args, fmt);
	vsprintf(buf, fmt, args);
	va_end(args);
	tprintf("%s\n", buf);
}

void node_perror(char *str, int err)
{
	int oldmode;

	oldmode = set_eolmode(User.fd, EOLMODE_TEXT);
	buf[0] = 0;
	if (str)
		strcpy(buf, str);
	if (str && err != -1)
		strcat(buf, ": ");
	if (err != -1)
		strcat(buf, strerror(err));
	tprintf("%s} %s\n", NodeId, buf);
	usflush(User.fd);
	set_eolmode(User.fd, oldmode);
	fpaclog(LOGLVL_ERROR, buf);
}

char *print_node(const char *alias, const char *call)
{
	static char node[17];

	sprintf(node, "%6s%c%s",
		!strcmp(alias, "*") ? "" : alias,
		!strcmp(alias, "*") ? ' ' : ':',
		call);
	return node;
}

void fpaclog(int loglevel, const char *fmt, ...)
{
	va_list args;
	int pri;
	static int opened = 0;

	if (LogLevel < loglevel)
		return;
	if (!opened) {
		openlog("node", LOG_PID, LOG_LOCAL7);
		opened = 1;
	}
	switch (loglevel) {
	case LOGLVL_ERROR:
		pri = LOG_ERR;
		break;
	case LOGLVL_LOGIN:
		pri = LOG_NOTICE;
		break;
	case LOGLVL_GW:
		pri = LOG_INFO;
		break;
	default:
		pri = LOG_INFO;
		break;
	}
	va_start(args, fmt);
	vsprintf(buf, fmt, args);
	syslog(pri, "%s\n", buf);
	va_end(args);
}


/*
 * F6BVP 2026-09-19: is an AX.25 link to <call> stuck in state 1 (awaiting
 * connection) after at least one unanswered SABM?  /proc/net/ax25 rows are
 *   ptr dev src dest st vs vr va t1timer t1 t2timer t2 t3timer t3 idle idlemax n2count n2 ...
 * Returns 1 and fills *n2count / *n2 (retries done / allowed) if so. The
 * kernel only counts a NetRom neighbour failure once all retries are used,
 * so nr_neigh "failed" stays 0 for the whole first cycle (about ten
 * minutes with the linear T1 back-off).
 */
int nr_link_pending(const char *call, int *n2count, int *n2)
{
	FILE *fp;
	char line[512], ptr[24], dev[16], src[16], dst[16];
	int st, v[13];
	int found = 0;

	/* v[] = vs vr va t1timer t1 t2timer t2 t3timer t3 idle idlemax
	 *       n2count n2 */
	if ((fp = fopen("/proc/net/ax25", "r")) == NULL)
		return 0;
	while (fgets(line, sizeof(line), fp) != NULL)
	{
		if (sscanf(line, "%23s %15s %15s %15s %d %d %d %d %d %d %d %d %d %d %d %d %d %d",
			   ptr, dev, src, dst, &st,
			   &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6],
			   &v[7], &v[8], &v[9], &v[10], &v[11], &v[12]) != 18)
			continue;
		if (strcasecmp(dst, call) == 0 && st == 1 && v[11] >= 1)
		{
			if (n2count)
				*n2count = v[11];
			if (n2)
				*n2 = v[12];
			found = 1;
			break;
		}
	}
	fclose(fp);
	return found;
}
