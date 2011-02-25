LDADD = $(AX25_LIB) $(MATH_LIB) ../lib/libfpac.a 
sbin_PROGRAMS = calibrate remote aprslist mailbbs fpacwdog
calibrate_SOURCES = calibrate.c
remote_SOURCES = remote.c
aprslist_SOURCES = aprslist.c
mailbbs_SOURCES = mailbbs.c
fpacwdog_SOURCES = fpacwdog.c

AM_CPPFLAGS = -DAX25_SYSCONFDIR=\""$(sysconfdir)/ax25/"\" \
              -DAX25_LOCALSTATEDIR=\""$(localstatedir)/ax25/"\" \
	      -I$(srcdir)/../lib 

AX25_SYSCONFDIR=@sysconfdir@/ax25/
AX25_LOCALSTATEDIR=@localstatedir@/ax25/

