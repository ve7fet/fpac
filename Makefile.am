installconf:
	(cd $(top_srcdir)/etc && $(MAKE) installconf)

SUBDIRS = lib cfg man monitor node flex pad tcp wp tools stats doc help scripts etc

install-data-hook:
	@echo
	@echo " ***  Read the \`README' if you haven't already done so!"
	@echo " ***  "
	@echo " ***  Remember to run \`make installconf' if this is a new installation."
	@echo " ***  Don't forget to:"
	@echo " ***    echo \"fpad 10093/tcp # FPAC\" >> /etc/services"
	@echo " ***    echo \"fpad 10093/udp # FPAC\" >> /etc/services"
	@echo " ***  If this is a new installation to add the required ports"
	@echo " ***  "
	@echo " ***  Be sure to edit \`SYSOP=' and \`CONNECT=' in fpac.sh"
	@echo " ***  according to your own BBS callsign and sysop name."
	@echo

EXTRA_DIST = pathnames.h fpac.spec

ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = -DAX25_SYSCONFDIR=\""$(sysconfdir)/ax25/"\" \
              -DAX25_LOCALSTATEDIR=\""$(localstatedir)/ax25/"\"

AX25_SYSCONFDIR=@sysconfdir@/ax25/
AX25_LOCALSTATEDIR=@localstatedir@/ax25/

