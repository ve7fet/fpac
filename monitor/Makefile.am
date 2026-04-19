LDADD = $(AX25_LIB) $(NCURSES_LIB)
sbin_PROGRAMS = monitor
monitor_SOURCES = listen.c listen.h kissdump.c ax25dump.c nrdump.c arpdump.c ipdump.c icmpdump.c udpdump.c tcpdump.c rspfdump.c ripdump.c rosedump.c flexnetdump.c utils.c

AM_CPPFLAGS = -DAX25_SYSCONFDIR=\""$(sysconfdir)/ax25/"\" \
              -DAX25_LOCALSTATEDIR=\""$(localstatedir)/ax25/"\" \
              -I$(srcdir)/../lib

AX25_SYSCONFDIR=@sysconfdir@/ax25/
AX25_LOCALSTATEDIR=@localstatedir@/ax25/

