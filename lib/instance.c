/******************************************************
 *  instance.c                                        *
 * FPAC project.  Only one copy of each daemon        *
 *                                                    *
 * Kept apart from procutils.c so that flexd, which   *
 * has its own copy of the FlexNet readers, can link  *
 * it alone.                                          *
 ******************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/file.h>

#include "instance.h"

/*
 * Single instance lock of the FPAC daemons (fpad, fpacwpd, fpacstat,
 * fpacroute, flexd, aprslist). A second copy of one of them would listen
 * on the same addresses as the running one, and a second fpad would also
 * clear and rebuild the ROSE routes of the node. flexd would poll its
 * FlexNet gateway twice, aprslist would send each beacon twice.
 *
 * The lock (flock on <localstatedir>/ax25/fpac/<prog>.lock) is held for the
 * whole life of the process and released by the kernel when it dies, whatever
 * the way. It goes through fork(), so that it survives daemon_start(); it is
 * not inherited by an executed program, and a child that keeps running the
 * same code must call fpac_single_instance_close().
 *
 * When the lock file cannot be opened at all (directory missing, no right
 * to read it), the daemon logs a warning and starts without the protection.
 */
static int instance_fd = -1;

static char *instance_file(const char *prog)
{
	static char name[256];

	snprintf(name, sizeof(name), "%sfpac/%s.lock", AX25_LOCALSTATEDIR, prog);
	return name;
}

/* Returns 0 when this process may run, -1 when another one already does */
int fpac_single_instance(const char *prog)
{
	char buf[32];
	char *name = instance_file(prog);
	int n;

	/* A lock does not need a writable file: when the file system is full
	 * or read-only, an existing lock file still does the job */
	if ((instance_fd = open(name, O_RDWR | O_CREAT | O_CLOEXEC, 0644)) < 0)
		instance_fd = open(name, O_RDONLY | O_CLOEXEC);
	if (instance_fd < 0)
	{
		/* No lock is better than no node */
		syslog(LOG_WARNING, "%s: cannot open %s: %s", prog, name, strerror(errno));
		return 0;
	}
	if (flock(instance_fd, LOCK_EX | LOCK_NB) != 0)
	{
		n = read(instance_fd, buf, sizeof(buf) - 1);
		buf[n > 0 ? n : 0] = 0;
		fprintf(stderr, "%s: another %s is already running (pid %d): not started\n", prog, prog, atoi(buf));
		syslog(LOG_ERR, "%s: another %s is already running (pid %d): not started", prog, prog, atoi(buf));
		close(instance_fd);
		instance_fd = -1;
		return -1;
	}
	return 0;
}

/* The pid in the lock file only names the holder in the message above; it is
 * written once the final process (after daemon_start) is known. */
void fpac_single_instance_pid(const char *prog)
{
	char buf[32];
	int fd, n;

	if (instance_fd < 0)
		return;
	if ((fd = open(instance_file(prog), O_WRONLY | O_TRUNC)) < 0)
		return;
	n = snprintf(buf, sizeof(buf), "%d\n", (int) getpid());
	if (write(fd, buf, n) != n)
		syslog(LOG_WARNING, "%s: cannot write %s: %s", prog, instance_file(prog), strerror(errno));
	close(fd);
}

/* For a forked child that goes on without exec: it must not keep the lock
 * of its parent alive */
void fpac_single_instance_close(void)
{
	if (instance_fd >= 0)
		close(instance_fd);
	instance_fd = -1;
}
