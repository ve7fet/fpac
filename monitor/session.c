/*
 * session.c (monitor)
 * FPAC project
 *
 * F6BVP 2026-10-16: monitor run from fpacnode (Lis, MONitor) writes on
 * the socket of the user's own session, which fpacnode hands over as
 * standard output. The frames carrying that output were captured and
 * displayed in turn, producing more frames: the display ran away on
 * a packet session. The session is identified once, at start, from
 * file descriptor 1, and its frames are left out:
 *   AX.25:  frames between the local and the remote callsign;
 *   NET/ROM: frames between the two nodes of the circuit, carrying
 *           either circuit index/id of the session (/proc/net/nr);
 *   ROSE:   frames to or from the neighbour of the session, carrying
 *           its logical channel (/proc/net/rose, /proc/net/rose_neigh);
 *   and for NET/ROM and ROSE, the supervisory frames (RR, RNR, REJ) of
 *   the AX.25 link that carries the session, once a frame of the
 *   session has shown which link it is: they acknowledge its frames
 *   and kept the display running on their own (f6bvp-12, 16 Oct 2026).
 * Anything else (terminal, telnet) leaves every frame displayed, and
 * so does a session that cannot be identified.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <netax25/ax25.h>
#include <netax25/axlib.h>

#include "listen.h"

#define AXLEN		7
#define HDLCAEB		0x01
#define PID_NETROM	0xCF
#define PID_X25		0x01

static int sess_family = AF_UNSPEC;
static ax25_address sess_a, sess_b;	/* AX.25 calls, NET/ROM nodes, ROSE neighbour */
static unsigned char nr_my[2], nr_your[2];
static unsigned int rose_lci;

/*
 * NET/ROM and ROSE: the AX.25 link that carries the session, learnt
 * from its first frame seen. The supervisory frames of that link (RR,
 * RNR, REJ) acknowledge the session's frames too and carry no data:
 * shown, they kept the display running away on their own.
 */
static int link_known;
static ax25_address link_x, link_y;

/* Inode of the socket on standard output, 0 if it is not a socket */
static ino_t stdout_inode(void)
{
	struct stat st;

	if (fstat(STDOUT_FILENO, &st) == -1 || !S_ISSOCK(st.st_mode))
		return 0;
	return st.st_ino;
}

static int find_netrom(ino_t ino)
{
	char line[256], user[16], dest[16], src[16], dev[16];
	unsigned int my_i, my_id, ur_i, ur_id;
	unsigned long inode;
	FILE *fp;
	int found = 0;

	if ((fp = fopen("/proc/net/nr", "r")) == NULL)
		return 0;
	if (fgets(line, sizeof(line), fp) == NULL) {	/* header */
		fclose(fp);
		return 0;
	}
	while (!found && fgets(line, sizeof(line), fp) != NULL) {
		const char *p = strrchr(line, ' ');

		if (p == NULL || sscanf(p, "%lu", &inode) != 1 || inode != ino)
			continue;
		if (sscanf(line, "%15s %15s %15s %15s %x/%x %x/%x", user, dest,
			   src, dev, &my_i, &my_id, &ur_i, &ur_id) != 8)
			break;
		if (ax25_aton_entry(src, (char *)sess_a.ax25_call) == -1 ||
		    ax25_aton_entry(dest, (char *)sess_b.ax25_call) == -1)
			break;
		nr_my[0] = my_i;
		nr_my[1] = my_id;
		nr_your[0] = ur_i;
		nr_your[1] = ur_id;
		found = 1;
	}
	fclose(fp);
	return found;
}

static int find_rose(ino_t ino)
{
	char line[256], call[16];
	unsigned int lci, neigh = 0, number;
	unsigned long inode;
	FILE *fp;
	int found = 0;

	if ((fp = fopen("/proc/net/rose", "r")) == NULL)
		return 0;
	if (fgets(line, sizeof(line), fp) == NULL) {
		fclose(fp);
		return 0;
	}
	while (!found && fgets(line, sizeof(line), fp) != NULL) {
		const char *p = strrchr(line, ' ');

		if (p == NULL || sscanf(p, "%lu", &inode) != 1 || inode != ino)
			continue;
		/* dest_addr dest_call src_addr src_call dev lci neigh ... */
		if (sscanf(line, "%*s %*s %*s %*s %*s %x %u", &lci, &neigh) == 2)
			found = 1;
		break;
	}
	fclose(fp);
	if (!found || neigh == 0)
		return 0;

	/* The neighbour number gives its callsign */
	if ((fp = fopen("/proc/net/rose_neigh", "r")) == NULL)
		return 0;
	found = 0;
	if (fgets(line, sizeof(line), fp) != NULL) {
		while (fgets(line, sizeof(line), fp) != NULL) {
			if (sscanf(line, "%u %15s", &number, call) != 2 ||
			    number != neigh)
				continue;
			if (ax25_aton_entry(call, (char *)sess_a.ax25_call) == 0) {
				rose_lci = lci;
				found = 1;
			}
			break;
		}
	}
	fclose(fp);
	return found;
}

/* Returns 1 when the session on standard output was identified */
int session_init(void)
{
	struct full_sockaddr_ax25 local, peer;
	socklen_t len;
	struct sockaddr sa;
	ino_t ino;

	if ((ino = stdout_inode()) == 0)
		return 0;

	len = sizeof(sa);
	if (getsockname(STDOUT_FILENO, &sa, &len) == -1)
		return 0;

	switch (sa.sa_family) {
	case AF_AX25:
		len = sizeof(local);
		if (getsockname(STDOUT_FILENO, (struct sockaddr *)&local, &len) == -1)
			return 0;
		len = sizeof(peer);
		if (getpeername(STDOUT_FILENO, (struct sockaddr *)&peer, &len) == -1)
			return 0;
		sess_a = local.fsa_ax25.sax25_call;
		sess_b = peer.fsa_ax25.sax25_call;
		sess_family = AF_AX25;
		return 1;
	case AF_NETROM:
		if (find_netrom(ino)) {
			sess_family = AF_NETROM;
			return 1;
		}
		return 0;
	case AF_ROSE:
		if (find_rose(ino)) {
			sess_family = AF_ROSE;
			return 1;
		}
		return 0;
	default:
		return 0;
	}
}

static int same_call(const unsigned char *frame, const ax25_address *call)
{
	return ax25_cmp((const ax25_address *)frame, call) == 0;
}

/*
 * Returns 1 when the frame (as received on a packet socket, KISS byte
 * first) belongs to the session on standard output.
 */
int session_frame(const unsigned char *buf, int len)
{
	const unsigned char *dst, *src, *p;
	int n, ndigis = 0;
	unsigned char ctl;

	if (sess_family == AF_UNSPEC)
		return 0;

	/* KISS data frame only, then destination and source */
	if (len < 1 + AXLEN + AXLEN + 1 || (buf[0] & 0x0F) != 0)
		return 0;
	dst = buf + 1;
	src = dst + AXLEN;
	p = src + AXLEN;
	n = len - 1 - 2 * AXLEN;

	if (sess_family == AF_AX25)
		return (same_call(src, &sess_a) && same_call(dst, &sess_b)) ||
		       (same_call(src, &sess_b) && same_call(dst, &sess_a));

	/* NET/ROM and ROSE: one neighbour is the ROSE neighbour; skip the
	 * digipeaters, then the control field. */
	if (sess_family == AF_ROSE &&
	    !same_call(src, &sess_a) && !same_call(dst, &sess_a))
		return 0;
	if (!(src[AXLEN - 1] & HDLCAEB)) {
		do {
			if (n < AXLEN + 1 || ++ndigis > 8)
				return 0;
			p += AXLEN;
			n -= AXLEN;
		} while (!(p[-1] & HDLCAEB));
	}
	if (n < 1)
		return 0;
	ctl = p[0];
	if ((ctl & 0x03) == 0x01)		/* S frame: RR, RNR, REJ */
		return link_known &&
		       ((same_call(src, &link_x) && same_call(dst, &link_y)) ||
			(same_call(src, &link_y) && same_call(dst, &link_x)));
	if ((ctl & 0x01) != 0 && (ctl & 0xEF) != 0x03)	/* neither I nor UI */
		return 0;
	if (n < 2)
		return 0;

	if (sess_family == AF_ROSE) {
		/* X.25 packet header: GFI and LCI */
		if (p[1] != PID_X25 || n < 4)
			return 0;
		if ((((p[2] & 0x0F) << 8) | p[3]) != rose_lci)
			return 0;
	} else {
		/* NET/ROM: origin and destination nodes, ttl, then the circuit */
		if (p[1] != PID_NETROM || n < 2 + AXLEN + AXLEN + 1 + 5)
			return 0;
		p += 2;
		if (!((same_call(p, &sess_a) && same_call(p + AXLEN, &sess_b)) ||
		      (same_call(p, &sess_b) && same_call(p + AXLEN, &sess_a))))
			return 0;
		p += AXLEN + AXLEN + 1;
		if (!((p[0] == nr_my[0] && p[1] == nr_my[1]) ||
		      (p[0] == nr_your[0] && p[1] == nr_your[1])))
			return 0;
	}

	/* A frame of the session: remember the link that carries it */
	memcpy(&link_x, src, sizeof(link_x));
	memcpy(&link_y, dst, sizeof(link_y));
	link_known = 1;
	return 1;
}
