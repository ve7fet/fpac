/*
 * wp/wptime32.c
 *
 * FPAC project
 *
 * Conversion of a White Pages file written with a 32-bit time_t.
 *
 * wp_t starts with a time_t. A 32-bit system that moves to a 64-bit time_t
 * (Debian 13 "trixie" on armhf, for instance) gets records of 152 bytes
 * instead of 144, so fpacwpd rejected the old file ("size error") and
 * replaced it with an empty database, and wpmaint misread it. The other
 * fields have the same size and alignment on every Linux ABI, so the old
 * layout is wp_t with a 32-bit date.
 *
 * F6BVP 2026-10-24
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "ax25compat.h"
#include "wp.h"
#include "wptime32.h"

typedef struct {
	int32_t date;
	struct full_sockaddr_rose address;
	char name[22];
	char city[22];
	char locator[7];
	char is_node;
	char is_deleted;
	unsigned char del_date[4];
	char free[19];
} wp32_t;

_Static_assert(sizeof(wp32_t) == 144, "unexpected size of the 32-bit time_t White Pages record");

static void report(int prio, const char *fmt, const char *path, const char *more)
{
	syslog(prio, fmt, path, more);
	fprintf(stderr, fmt, path, more);
	fputc('\n', stderr);
}

int wp_convert_time32(const char *path)
{
	char tmp[300], keep[300], stamp[32];
	struct stat st;
	wp_header h;
	wp32_t o;
	wp_t n;
	long slots, i;
	time_t now;
	int in, out = -1;

	/* A 32-bit time_t build reads such a file as it is */
	if (sizeof(wp_t) == sizeof(wp32_t))
		return 0;

	in = open(path, O_RDONLY);
	if (in < 0)
		return 0;
	if (fstat(in, &st) < 0 || read(in, &h, sizeof(h)) != sizeof(h) ||
	    strncmp(h.signature, FILE_SIGNATURE, sizeof(h.signature)) != 0 ||
	    st.st_size < (off_t)sizeof(h)) {
		close(in);
		return 0;
	}
	/* Right layout already, or a layout this code does not know */
	if ((st.st_size - sizeof(h)) % sizeof(wp_t) == 0 ||
	    (st.st_size - sizeof(h)) % sizeof(wp32_t) != 0) {
		close(in);
		return 0;
	}
	slots = (st.st_size - sizeof(h)) / sizeof(wp32_t);
	if (h.nb_record < 0 || h.nb_record > slots) {
		close(in);
		return 0;
	}

	now = time(NULL);
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", localtime(&now));
	snprintf(tmp, sizeof(tmp), "%s.time64-tmp", path);
	snprintf(keep, sizeof(keep), "%s.time32-%s", path, stamp);

	out = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out < 0)
		goto fail;
	if (write(out, &h, sizeof(h)) != sizeof(h))
		goto fail;
	for (i = 0; i < slots; i++) {
		if (read(in, &o, sizeof(o)) != sizeof(o))
			goto fail;
		memset(&n, 0, sizeof(n));
		n.date = (time_t)o.date;
		n.address = o.address;
		memcpy(n.name, o.name, sizeof(n.name));
		memcpy(n.city, o.city, sizeof(n.city));
		memcpy(n.locator, o.locator, sizeof(n.locator));
		n.is_node = o.is_node;
		n.is_deleted = o.is_deleted;
		memcpy(n.del_date, o.del_date, sizeof(n.del_date));
		memcpy(n.free, o.free, sizeof(n.free));
		if (write(out, &n, sizeof(n)) != sizeof(n))
			goto fail;
	}
	if (fsync(out) < 0 || close(out) < 0) {
		out = -1;
		goto fail;
	}
	out = -1;
	close(in);

	/* Keep the old file, then put the converted one in its place */
	if (rename(path, keep) < 0) {
		report(LOG_ERR, "WP file %s: cannot keep the 32-bit time_t file (%s)", path, strerror(errno));
		unlink(tmp);
		return -1;
	}
	if (rename(tmp, path) < 0) {
		report(LOG_ERR, "WP file %s: cannot install the converted file (%s)", path, strerror(errno));
		rename(keep, path);
		unlink(tmp);
		return -1;
	}
	syslog(LOG_NOTICE, "WP file %s: converted from a 32-bit time_t layout (%d records), old file kept as %s",
	       path, h.nb_record, keep);
	fprintf(stderr, "WP file %s: converted from a 32-bit time_t layout (%d records), old file kept as %s\n",
		path, h.nb_record, keep);
	return 1;

fail:
	report(LOG_ERR, "WP file %s: conversion from a 32-bit time_t layout failed (%s), file left untouched",
	       path, strerror(errno));
	if (out >= 0)
		close(out);
	close(in);
	unlink(tmp);
	return -1;
}
