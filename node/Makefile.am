LDADD = $(AX25_LIB) ../lib/libfpac.a
sbin_PROGRAMS = fpacnode yapp fpacshell
fpacnode_SOURCES = fpacnode.c fpaccmd.c nodesys.c command.c extcmd.c gateway.c lang.c user.c util.c io.c io.h sysinfo.c sysinfo.h node.h colors.c colors.h
yapp_SOURCES = yapp.c node.h 
fpacshell_SOURCES = fpacshell.c

AM_CPPFLAGS = -DAX25_SYSCONFDIR=\""$(sysconfdir)/ax25/"\" \
              -DAX25_LOCALSTATEDIR=\""$(localstatedir)/ax25/"\" \
	      -I$(srcdir)/../lib 

AX25_SYSCONFDIR=@sysconfdir@/ax25/
AX25_LOCALSTATEDIR=@localstatedir@/ax25/


# fpacnode binds its NetRom socket under the callsign of the user, which the
# kernel only allows with CAP_NET_BIND_SERVICE. Give fpacnode that capability
# so that a session of an ordinary account (fpacuser, a sysop without sudo)
# can call NetRom nodes. Copying the file over fpacnode drops it again.
# Skipped for a staged install (DESTDIR): see debian/fpac.postinst.
install-exec-hook:
	@if test -n "$(DESTDIR)"; then :; \
	elif command -v setcap >/dev/null 2>&1 && \
	     setcap cap_net_bind_service=ep $(sbindir)/fpacnode 2>/dev/null; then \
		echo "fpacnode: cap_net_bind_service set"; \
	else \
		echo "WARNING: cannot give cap_net_bind_service to $(sbindir)/fpacnode."; \
		echo "         NetRom calls from a session that is not root will fail."; \
		echo "         As root: setcap cap_net_bind_service=ep $(sbindir)/fpacnode"; \
	fi
