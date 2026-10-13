docdir = $(datadir)/doc/@PACKAGE@
doc_DATA = 	FPAC-HOWTO.man \
		FPAC-HOWTO.sgml \
		FPAC-HOWTO.txt \
		aprslist \
		aprslist.txt \
		call_tcp.txt \
		mailbbs.txt \
		calibrate.txt \
		ax25 \
		ax25-up \
		ax25-down \
		README.init

install-data-hook:
	chmod 755 $(DESTDIR)$(docdir)/ax25
	chmod 755 $(DESTDIR)$(docdir)/ax25-up
	chmod 755 $(DESTDIR)$(docdir)/ax25-down

SUBDIRS = html

EXTRA_DIST = $(doc_DATA)

