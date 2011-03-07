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
