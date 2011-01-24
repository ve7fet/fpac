LDADD = $(AX25_LIB) ../lib/libfpac.a
sbin_PROGRAMS = call_tcp calld mon_tcp
call_tcp_SOURCES = call_tcp.c
calld_SOURCES = calld.c
mon_tcp_SOURCES = mon_tcp.c

AM_CPPFLAGS = -DAX25_SYSCONFDIR=\""$(sysconfdir)/ax25/"\" \
              -DAX25_LOCALSTATEDIR=\""$(localstatedir)/ax25/"\" \
	      -I$(srcdir)/../lib 

AX25_SYSCONFDIR=@sysconfdir@/ax25/
AX25_LOCALSTATEDIR=@localstatedir@/ax25/

