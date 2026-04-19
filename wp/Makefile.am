LDADD = $(AX25_LIB) ../lib/libfpac.a
noinst_PROGRAMS = make_tab
sbin_PROGRAMS = fpacwpd wpedit wplist wpstress wpmaint wpserv wpcmp wpconvert
make_tab_SOURCES = make_tab.c
fpacwpd_SOURCES = fpacwpd.c daemon.c daemon.h db.c db.h db_list.c update.c update.h hash.c hash.h crc.c crc.h sockevent.c sockevent.h wpdefs.h
wplist_SOURCES = wplist.c
wpedit_SOURCES = wpedit.c
wpstress_SOURCES = wpstress.c
wpmaint_SOURCES = wpmaint.c
wpserv_SOURCES = wpserv.c
wpcmp_SOURCES = wpcmp.c crc.c
wpconvert_SOURCES = wpconvert.c

BUILT_SOURCES = crc_tab.h

crc_tab.h:	make_tab
	./make_tab > crc_tab.h

CLEANFILES = crc_tab.h

AM_CPPFLAGS = -DAX25_SYSCONFDIR=\""$(sysconfdir)/ax25/"\" \
              -DAX25_LOCALSTATEDIR=\""$(localstatedir)/ax25/"\" \
              -I$(srcdir)/../lib

AX25_SYSCONFDIR=@sysconfdir@/ax25/
AX25_LOCALSTATEDIR=@localstatedir@/ax25/

