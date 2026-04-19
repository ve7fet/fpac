LDADD = $(AX25_LIB) ../lib/libfpac.a
sbin_PROGRAMS = fpacnode yapp fpacshell
fpacnode_SOURCES = fpacnode.c fpaccmd.c nodesys.c command.c extcmd.c gateway.c user.c util.c io.c io.h sysinfo.c sysinfo.h node.h 
yapp_SOURCES = yapp.c node.h 
fpacshell_SOURCES = fpacshell.c

AM_CPPFLAGS = -DAX25_SYSCONFDIR=\""$(sysconfdir)/ax25/"\" \
              -DAX25_LOCALSTATEDIR=\""$(localstatedir)/ax25/"\" \
	      -I$(srcdir)/../lib 

AX25_SYSCONFDIR=@sysconfdir@/ax25/
AX25_LOCALSTATEDIR=@localstatedir@/ax25/

