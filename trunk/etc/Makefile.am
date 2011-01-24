etcfiles = fpac.dnic fpac.info fpac.hello itu.dnic aprslist.conf aprslist.conf.fr flexd.conf flexd.routes fpac.conf fpac.nodes fpac.routes

installconf:
	$(mkinstalldirs) $(DESTDIR)$(AX25_SYSCONFDIR)
	@list='$(etcfiles)'; for p in $$list; do \
	  echo " $(INSTALL_DATA) $$p $(DESTDIR)$(AX25_SYSCONFDIR)/$$p"; \
	  $(INSTALL_DATA) $$p $(DESTDIR)$(AX25_SYSCONFDIR)/$$p; \
	done


AX25_SYSCONFDIR=$(sysconfdir)/ax25

EXTRA_DIST = $(etcfiles)

