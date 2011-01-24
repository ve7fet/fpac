noinst_SCRIPTS = do_subst

etcfiles = \
	fpac.dnic \
	fpac.info \
	fpac.hello \
	itu.dnic \
	aprslist.conf \
	aprslist.conf.fr \
	flexd.conf \
	flexd.routes \
	fpac.conf \
	fpac.nodes \
	fpac.routes

installconf:
	$(mkinstalldirs) $(DESTDIR)$(AX25_SYSCONFDIR)
	@list='$(etcfiles)'; for p in $$list; do \
	  echo " $(INSTALL_DATA) $$p $(DESTDIR)$(AX25_SYSCONFDIR)/$$p"; \
	  $(INSTALL_DATA) $$p $(DESTDIR)$(AX25_SYSCONFDIR)/$$p; \
	done

edit = sed \
	-e 's|@sbindir[@]|$(sbindir)|g' \
	-e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@localstatedir[@]|$(localstatedir)|g' \
	-e 's|@prefix[@]|$(prefix)|g'

do_subst:
	@list='$(etcfiles)'; for p in $$list; do \
	$(edit) $(srcdir)/$$p.in >$$p; \
	done

AX25_SYSCONFDIR=$(sysconfdir)/ax25

CLEANFILES = $(etcfiles)

EXTRA_DIST = \
	fpac.dnic.in \
	fpac.info.in \
	fpac.hello.in \
	itu.dnic.in \
	aprslist.conf.in \
	aprslist.conf.fr.in \
	flexd.conf.in \
	flexd.routes.in \
	fpac.conf.in \
	fpac.nodes.in \
	fpac.routes.in

