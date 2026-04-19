/*
 * node/io.c
 *
 * FPAC project
 *
 * This is an I/O library for file descriptors (or sockets). It is
 * sort of like stdio but a few differences. All reads and writes
 * are buffered with user defined buffer sizes (ie. reading and
 * writing to SOCK_SEQPACKET sockets is safe). Also this library
 * provides user selectable end-of-line conversions and very limited
 * telnet option negotiation (mostly just answering no to all proposals).
 *
 * I'm not at all sure this is a good idea and parts of the lib (like
 * the telnet stuff) are a mess but I needed something for my AX.25
 * projects. So here it is.
 *
 * After you get a file descriptor from open() or connect() you call
 * init_io(). Then you can use usputc(), usprintf() etc. to write to and
 * usgetc, usgets() etc. to read from the file descriptor. usflush()
 * causes any pending output to be sent even if the buffer isn't full
 * and end_io() flushes the file descriptor and frees all buffers.
 *
 * Tomi Manninen, OH2BNS, <tomi.manninen@hut.fi>
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
/*#include <sys/types.h>*/
/*#include <sys/socket.h>*/
#include <unistd.h>

#include "io.h"

#define StdoutFd	STDIN_FILENO
#define BUFLEN		1500

/* Telnet command characters */
#define IAC			255	/* Interpret as command */
#define WILL			251
#define WONT			252
#define DO			253
#define DONT			254
#define SB			250	/* Subnegotiation begin */
#define SE			240	/* Subnegotiation end   */

/* Telnet options */
#define TN_TRANSMIT_BINARY	0
#define TN_ECHO			1
#define TN_SUPPRESS_GA		3
#define TN_STATUS		5
#define TN_TIMING_MARK		6

/* Linemode options */
#define TN_LINEMODE		34
#define TN_LINEMODE_MODE	1
#define TN_LINEMODE_MODE_EDIT	1

struct io {
	int fd;			/* socket index				*/
	char *eol;		/* end-of-line sequence			*/
	int eolmode;		/* end-of-line translation on/off	*/
	int telnetmode;		/* telnet option negotiation on/off	*/
	int tn_echo;		/* will/wont echo			*/
	int tn_linemode;	/* will/wont linemode			*/
	int size;		/* size of the packet in input buffer	*/
	int paclen;		/* paclen				*/
	unsigned char *ibuf;	/* input buffer				*/
	unsigned char *obuf;	/* output buffer			*/
	int ipointer;		/* input pointer			*/
	int opointer;		/* output pointer			*/

	struct io *next;
};

static struct io *Iolist = NULL;

static struct io *itop(int fd)
{
	struct io *iop;

	for (iop = Iolist; iop != NULL && iop->fd != fd; iop = iop->next)
		;
	if (iop == NULL)
		errno = 10000; /* "unknown error 10000" */
	return iop;
}

int init_io(int fd, int paclen, char *eol)
{
	struct io *new;

	if (itop(fd) != NULL)
		return -1;
	if ((new = malloc(sizeof(struct io))) == NULL)
		return -1;
	new->ibuf	= malloc(BUFLEN);
	new->obuf	= malloc(paclen);
	new->fd		= fd;
	new->eol	= eol;
	new->eolmode	= EOLMODE_TEXT;
	new->telnetmode = 0;
	new->tn_echo	= 0;
	new->tn_linemode = 0;
	new->size	= 0;
	new->paclen	= paclen;
	new->ipointer	= 0;
	new->opointer	= 0;
	if (new->ibuf == NULL || new->obuf == NULL) {
		free(new->ibuf);
		free(new->obuf);
		free(new);
		return -1;
	}
	new->next = Iolist;
	Iolist = new;
	return 0;
}

int set_paclen(int fd, int paclen)
{
	struct io *iop;
	unsigned char *old_obuf;

	usflush(fd);
	if ((iop = itop(fd)) == NULL)
		return -1;
	if (paclen == iop->paclen)
		return paclen;
	old_obuf = iop->obuf;
	if ((iop->obuf = malloc(paclen)) == NULL) {
		iop->obuf = old_obuf;
		return -1;
	}
	free(old_obuf);
	iop->paclen = paclen;
	return paclen;
}

int set_eolmode(int fd, int newmode)
{
	struct io *iop;
	int oldmode;

	if ((iop = itop(fd)) == NULL)
		return -1;
	oldmode = iop->eolmode;
	iop->eolmode = newmode;
	return oldmode;
}

int set_telnetmode(int fd, int newmode)
{
	struct io *iop;
	int oldmode;

	if ((iop = itop(fd)) == NULL)
		return -1;
	oldmode = iop->telnetmode;
	iop->telnetmode = newmode;
	return oldmode;
}

void end_io(int fd)
{
	struct io *iop;

	usflush(fd);
	sleep(1);
	if ((iop = itop(fd)) == NULL)
		return;
	free(iop->ibuf);
	free(iop->obuf);
	iop->fd = -1;
	return;
}

int usflush(int fd)
{
	struct io *iop;
	int ret;
	
	if ((iop = itop(fd)) == NULL)
		return -1;
	if (iop->opointer == 0)
		return 0;
	ret = write(iop->fd, iop->obuf, iop->opointer);
	if (ret < 0)
		return -1;
	if (ret < iop->opointer) {
		memmove(iop->obuf, &iop->obuf[ret], iop->opointer - ret);
		iop->opointer = ret;
	} else
		iop->opointer = 0;
	return 0;
}

static int rsendchar(unsigned char c, struct io *iop)
{
	iop->obuf[iop->opointer++] = c;
	if (iop->opointer >= iop->paclen)
		return usflush(iop->fd);
	return 1;
}

static int rrecvchar(struct io *iop)
{
	if (iop->ipointer >= iop->size) {
		iop->size = read(iop->fd, iop->ibuf, BUFLEN);
		if (iop->size == -1)
			return -1;
		if (iop->size == 0) {
			errno = 0;
			return -1;
		}
		iop->ipointer = 0;
	}
	return iop->ibuf[iop->ipointer++];
}

int usputc(unsigned char c, int fd)
{
	struct io *iop;
	char *cp;

	if ((iop = itop(fd)) == NULL)
		return -1;
	if (iop->telnetmode && c == IAC) {
		if (rsendchar(IAC, iop) == -1)
			return -1;
		return rsendchar(IAC, iop);
	}
	if (iop->eolmode == EOLMODE_TEXT && c == '\n') {
		for (cp = iop->eol; *cp; cp++) {
			if (rsendchar(*cp, iop) == -1)
				return -1;
		}
		return 1;
	}
	return rsendchar(c, iop);
}

int usgetc(int fd)
{
	struct io *iop;
	int c, opt;
	char *cp;

	opt = 0; /* silence warning */
	if ((iop = itop(fd)) == NULL)
		return -1;
	if ((c = rrecvchar(iop)) == -1)
		return -1;
	if (iop->telnetmode && c == IAC) {
		if ((c = rrecvchar(iop)) == -1)
			return -1;
		if (c > 249 && c < 255 && (opt = rrecvchar(iop)) == -1)
			return -1;
		switch(c) {
		case SB:
			if ((c = rrecvchar(iop)) == -1)
				return -1;
			if ((opt = rrecvchar(iop)) == -1)
				return -1;
			while (!(c == IAC && opt == SE)) {
				c = opt;
				if ((opt = rrecvchar(iop)) == -1)
					return -1;
			}
			break;
		case WILL:
			if (opt == TN_LINEMODE && iop->tn_linemode) {
				rsendchar(IAC, iop);
				rsendchar(SB,  iop);
				rsendchar(TN_LINEMODE, iop);
				rsendchar(TN_LINEMODE_MODE, iop);
				rsendchar(TN_LINEMODE_MODE_EDIT, iop);
				rsendchar(IAC, iop);
				rsendchar(SE,  iop);
			} else {
				rsendchar(IAC,  iop);
				rsendchar(DONT, iop);
				rsendchar(opt,  iop);
			}
			usflush(fd);
			break;
		case DO:
			if (!(opt == TN_ECHO && iop->tn_echo)) {
				rsendchar(IAC,  iop);
				rsendchar(WONT, iop);
				rsendchar(opt,  iop);
				usflush(fd);
			}
			break;
		case IAC:	/* Escaped IAC */
			return IAC;
			break;
		case WONT:
		case DONT:
		default:
			break;
		}
		return usgetc(fd);
	}
	if (iop->eolmode == EOLMODE_TEXT && c == iop->eol[0]) {
		for (cp = iop->eol + 1; *cp; cp++) {
			if (rrecvchar(iop) == -1)
				return -1;
		}
		c = '\n';
	}
	return c;
}

char *usgets(char *buf, int buflen, int fd)
{
	int c, len = 0;

	while (len < (buflen - 1)) {
		c = usgetc(fd);
		if (c == -1) {
			if (len > 0) {
				buf[len] = 0;
				return buf;
			} else
				return NULL;
		}
		/* NUL also interpreted as EOL */
		if (c == '\n' || c == '\r' || c == 0) {
			buf[len] = 0;
			return buf;
		}
		buf[len++] = c;
	}
	buf[buflen - 1] = 0;
	return buf;
}

char *readline(int fd)
{
	static char buf[BUFLEN];

	return usgets(buf, BUFLEN, fd);
}

static int usvprintf(int fd, const char *fmt, va_list args)
{
	static char buf[BUFLEN];
	int len, i;

	len = vsprintf(buf, fmt, args);
	for (i = 0; i < len; i++)
		if (usputc(buf[i], fd) == -1)
			return -1;
	return len;
}

int usprintf(int fd, const char *fmt, ...)
{
	va_list args;
	int len;

	va_start(args, fmt);
	len = usvprintf(fd, fmt, args);
	va_end(args);
	return len;
}

int tprintf(const char *fmt, ...)
{
	va_list args;
	int len;

	va_start(args, fmt);
	len = usvprintf(StdoutFd, fmt, args);
	va_end(args);
	return len;
}

int usputs(const char *s, int fd)
{
	while (*s) {
		if (usputc(*s, fd) == -1)
			return -1;
		s++;
	}
	return 0;
}

int tputs(const char *s)
{
	while (*s) {
		if (usputc(*s, StdoutFd) == -1)
			return -1;
		s++;
	}
	return 0;
}

void tn_do_linemode(int fd)
{
	struct io *iop;

	if ((iop = itop(fd)) == NULL)
		return;
	rsendchar(IAC, iop);
	rsendchar(DO, iop);
	rsendchar(TN_LINEMODE, iop);
	iop->tn_linemode = 1;
}

void tn_will_echo(int fd)
{
	struct io *iop;

	if ((iop = itop(fd)) == NULL)
		return;
	rsendchar(IAC, iop);
	rsendchar(WILL, iop);
	rsendchar(TN_ECHO, iop);
	iop->tn_echo = 1;
}

void tn_wont_echo(int fd)
{
	struct io *iop;

	if ((iop = itop(fd)) == NULL)
		return;
	rsendchar(IAC, iop);
	rsendchar(WONT, iop);
	rsendchar(TN_ECHO, iop);
	iop->tn_echo = 0;
}
