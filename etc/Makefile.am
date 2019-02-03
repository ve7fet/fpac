BUILT_SOURCES = do_subst

AX25_SYSCONFDIR=$(sysconfdir)/ax25

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

install-data-hook:
	$(mkinstalldirs) $(DESTDIR)$(AX25_SYSCONFDIR)
	##On a new install, install the config files with their default names. 
	##On an upgeade, if we find files with the same name, install these ones 
	##with a .dist suffix, so we don't overwrite any user changed files.
	@for p in $(etcfiles); do \
	  if test -f $(DESTDIR)$(AX25_SYSCONFDIR)/$$p; then \
	  echo " $(DESTDIR)$(AX25_SYSCONFDIR)/$$p already exists; installing new file as $$p.dist"; \
	  $(INSTALL_DATA) $$p $(DESTDIR)$(AX25_SYSCONFDIR)/$$p.dist; \
	  else \
	  echo " Existing $$p not found; installing default $$p"; \
	  $(INSTALL_DATA) $$p $(DESTDIR)$(AX25_SYSCONFDIR)/$$p; \
	  fi; \
	done

#This makes distcheck happy by deleting all our files in /etc/ax25 on make uninstall
uninstall-hook:
	@for p in $(etcfiles); do \
	  $(RM) -f $(DESTDIR)$(AX25_SYSCONFDIR)/$$p; \
	  $(RM) -f $(DESTDIR)$(AX25_SYSCONFDIR)/$$p.dist; \
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

