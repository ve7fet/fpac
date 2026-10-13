
/******************************************************
 *  procutils.c                                       *
 * FPAC project.            Utilities                 *
 *                                                    *
 * Parts of code from different sources of ax25-utils *
 *                                                    *
 * F6FBB 05-1997                                      *
 *                                                    *
 ******************************************************/

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <sys/fcntl.h>
#include <sys/file.h>

#include "ax25compat.h"
#include "fpac.h"
#include "wp.h"

/*
 * Version of strncpy() that returns NULL if either src or dest is NULL
 * and also makes sure destination string is always terminated.
 */
static char *safe_strncpy(char *dest, char *src, int n)
{
	if (dest == NULL || src == NULL)
		return NULL;
	dest[n] = 0;
	return strncpy(dest, src, n);
}

/*
 * Version of atoi() that returns zero if s == NULL.
 */
static int safe_atoi(const char *s)
{
	return (s == NULL) ? 0 : atoi(s);
}

/*
 * Version of nr_config_load_ports() that do not report errors.
 */
void fpac_nr_config_load_ports(void)
{
	int fd = open("/dev/null", O_WRONLY);
	if (dup2(2, fd) != -1)
	{
		nr_config_load_ports();
		close(fd);
	}
}

/*
 *	fpac -> ascii conversion
 */
char *fpac2asc(rose_address * a)
{
	static char buf[12];

	sprintf(buf, "%02X%02X,%02X%02X%02X",
			a->rose_addr[0] & 0xFF,
			a->rose_addr[1] & 0xFF,
			a->rose_addr[2] & 0xFF,
			a->rose_addr[3] & 0xFF, a->rose_addr[4] & 0xFF);

	return buf;
}

char *rs_get_addr(char *dev)
{
	struct proc_rs *rp, *rlist;
	char *cp = (dev) ? dev : "rose0";
	static char add[11] = "          ";

	if ((rlist = read_proc_rs()) == NULL)
	{
		if (errno)
			perror("do_links: read_proc_rose");
		return NULL;
	}

	for (rp = rlist; rp != NULL; rp = rp->next)
	{
		if ((strcasecmp(rp->dest_addr, "*") == 0) &&
			(strcasecmp(rp->dest_call, "*") == 0) &&
			(strcasecmp(rp->dev, cp) == 0))
		{
			memcpy(add, rp->src_addr, 10);
			break;
		}
	}
	add[10] = '\0';
	free_proc_rs(rlist);
	return (add);
}

/*
 * F6BVP 2026-09-30: compare two callsigns as AX.25 does. By convention a
 * callsign written without SSID IS the callsign with SSID 0, and the
 * strings do not agree on that: the kernel (/proc/net/...) always writes
 * "-0", ax25_ntoa() never does, fpac.conf and user input may do either.
 * Comparing them as plain strings made "F3KT" and "F3KT-0" different.
 * Case-insensitive. Returns 1 when a and b are the same callsign.
 */
int callsign_eq(const char *a, const char *b)
{
	size_t la, lb;
	const char *sa, *sb;

	if (a == NULL || b == NULL)
		return 0;
	la = strcspn(a, "-");
	lb = strcspn(b, "-");
	if (la != lb || strncasecmp(a, b, la) != 0)
		return 0;
	sa = a[la] ? a + la + 1 : "0";
	sb = b[lb] ? b + lb + 1 : "0";
	return atoi(sa) == atoi(sb) && strspn(sa, "0123456789") == strlen(sa)
		&& strspn(sb, "0123456789") == strlen(sb);
}

/* is_heard : from awznode */
int is_heard(char **av)
{
	FILE *fp;
	static char port[14];
	static char dest[10];
	static char digi[10][AX25_MAX_DIGIS];
	struct mheard_struct mh;
	char call[12];
	char *cp;
	int k;

	if ((fp = fopen(DATA_MHEARD_FILE, "r")) == NULL) {
		printf("Error : file %s not found\n", DATA_MHEARD_FILE);
		return 0;
	}

	safe_strncpy(call, av[0], 9);
	(void)cp;

	while (fread(&mh, sizeof(struct mheard_struct), 1, fp) == 1)
	{
		/* ax25_ntoa() never writes "-0": "F4JQC" typed as "F4JQC" or
		 * "F4JQC-0" must match (it never did, see callsign_eq()) */
		if (callsign_eq(call, ax25_ntoa(&mh.from_call)))
		{
			fclose(fp);

			safe_strncpy(port, mh.portname, 13);
			safe_strncpy(dest, ax25_ntoa(&mh.from_call), 9);

			av[0] = port;
			av[1] = dest;

			for (k = 0; k < AX25_MAX_DIGIS; k++)
			{
				if (k <= mh.ndigis)
				{
					safe_strncpy(digi[k], ax25_ntoa(&mh.digis[k]), 9);
					av[2 + k] = digi[k];
				}
				else
				{
					av[2 + k] = NULL;
					break;
				}
			}
			return 1;
		}
	}

	fclose(fp);

	return 0;
}

/*
 * Flexnet routines taken from FlexNode
 */

struct flex_gt *read_flex_gt(void) {
    FILE *fp;
    char buffer[256], *cp;
    char *addr;
    struct flex_gt *p = NULL, *list = NULL, *new_el;
    int i = 0, k;
    errno = 0;
    if ((fp = fopen(FLEX_GT_FILE, "r")) == NULL) {
        printf("Error : file %s not found\n", FLEX_GT_FILE);
        return NULL;
    }

    while (fgets(buffer, 256, fp) != NULL) {
        if (i++ < 1)
            continue;
        if (buffer == NULL)
            continue;
        if ((new_el = calloc(1, sizeof (struct flex_gt))) == NULL)
            break;

        new_el->addr = safe_atoi(strtok(buffer, " \t\n\r"));
        safe_strncpy(new_el->call, strtok(NULL, " \t\n\r"), 9);
        safe_strncpy(new_el->dev, strtok(NULL, " \t\n\r"), 13);

        k = 0;
        while ((cp = strtok(NULL, " \t\n\r")) != NULL && k < AX25_MAX_DIGIS)
            safe_strncpy(new_el->digis[k++], cp, 9);
        while (k < AX25_MAX_DIGIS)
            strcpy(new_el->digis[k++], "\0");

        if ((addr = ax25_config_get_name(new_el->dev)) == NULL) {
            /*			nr_config_load_ports();*/

            if ((addr = nr_config_get_name(new_el->dev)) == NULL) {
                /*			rs_config_load_ports();*/
                if ((addr = rs_config_get_name(new_el->dev)) == NULL) {
                    fprintf(stderr,
                            "read_flex_gt: invalid port setting\n");
                    return NULL;
                } else {
                    new_el->af_mode = AF_ROSE;
                }
            } else {
                new_el->af_mode = AF_NETROM;
            }
        } else {
            new_el->af_mode = AF_FLEXNET;
        }

        if (list == NULL) {
            list = new_el;
            p = list;
        } else {
            p->next = new_el;
            p = p->next;
        }
    }
    fclose(fp);
    return list;
}

void free_flex_gt(struct flex_gt *fp)
{
	struct flex_gt *p;

	while (fp != NULL)
	{
		p = fp->next;
		free(fp);
		fp = p;
	}
}

struct flex_dst *read_flex_dst(void)
{
	FILE *fp;
	char buffer[256];
	struct flex_dst *p = NULL, *list = NULL, *new_el;
	int i = 0;

	errno = 0;
  	if ((fp = fopen(FLEX_DST_FILE, "r")) == NULL) {
/*		printf("Error : file %s not found\n", FLEX_DST_FILE);*/
	return NULL;
  	}

	while (fgets(buffer, 256, fp) != NULL)
	{
		if (i++ < 1)
			continue;
		if (buffer == NULL)
			continue;
		if ((new_el = calloc(1, sizeof(struct flex_dst))) == NULL)
			break;

		safe_strncpy(new_el->dest_call, strtok(buffer, " \t\n\r"), 9);
		new_el->ssida = safe_atoi(strtok(NULL, " -\t\n\r"));
		new_el->sside = safe_atoi(strtok(NULL, " -\t\n\r"));
		new_el->rtt = safe_atoi(strtok(NULL, " \t\n\r"));
		new_el->addr = safe_atoi(strtok(NULL, " \t\n\r"));

		if (list == NULL)
		{
			list = new_el;
			p = list;
		}
		else
		{
			p->next = new_el;
			p = p->next;
		}
	}
	fclose(fp);
	return list;
}

void free_flex_dst(struct flex_dst *fp)
{
	struct flex_dst *p;

	while (fp != NULL)
	{
		p = fp->next;
		free(fp);
		fp = p;
	}
}

struct flex_dst *find_dest(char *dest_call, struct flex_dst *list)
{
	static struct flex_dst f;
	struct flex_dst *fdst = NULL, *p;
	char *cp, call[10];
	int ssid;

	safe_strncpy(call, dest_call, 9);
	cp = strchr(call, '-');
	if (cp == NULL)
		ssid = 0;
	else
	{
		ssid = safe_atoi(cp + 1);
		*cp = '\0';
	}

	fdst = list ? list : read_flex_dst();
	for (p = fdst; p != NULL; p = p->next)
	{
		if (!strcasecmp(call, p->dest_call)
			&& (ssid >= p->ssida && ssid <= p->sside))
		{
			f = *p;
			f.next = NULL;
			p = &f;
			break;
		}
	}
	if (list == NULL)
		free_flex_dst(fdst);
	return p;
}

struct flex_gt *find_gateway(int addr, struct flex_gt *list)
{
	static struct flex_gt f;
	struct flex_gt *flgt = NULL, *p;

	flgt = list ? list : read_flex_gt();
	for (p = flgt; p != NULL; p = p->next)
	{
		if (addr == p->addr)
		{
			f = *p;
			f.next = NULL;
			p = &f;
			break;
		}
	}
	if (list == NULL)
		free_flex_gt(flgt);
	return p;
}

/*
 * Single instance lock of the FPAC daemons (fpad, fpacwpd, fpacstat,
 * fpacroute). A second copy of one of them would listen on the same
 * addresses as the running one, and a second fpad would also clear and
 * rebuild the ROSE routes of the node.
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
