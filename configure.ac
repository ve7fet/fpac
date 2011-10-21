AC_PREREQ(2.61)
AC_INIT(fpac, 3.27.19, f6bvp@free.fr)
AM_INIT_AUTOMAKE([-Wall -Werror dist-bzip2])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_SRCDIR([pathnames.h])

# Determine versions for package managers.
basever=$(echo $PACKAGE_VERSION | sed -e 's@-.*$@@')
AC_SUBST(BASEVERSION, [$basever])
extraver=$(echo $PACKAGE_VERSION | sed -e 's@'$basever'@@' -e 's@^-@_@')
AC_SUBST(EXTRAVERSION, [$extraver])

# Checks for programs.
AC_PROG_CC
AC_PROG_GCC_TRADITIONAL
AC_PROG_INSTALL
AC_PROG_LIBTOOL

# Checks for libraries.
AC_SUBST(AX25_LIB)
AC_SUBST(NCURSES_LIB)
AC_SUBST(MATH_LIB)
AC_CHECK_LIB(ax25, ax25_config_load_ports, AX25_LIB="-lax25", AC_MSG_ERROR([Could not find the libax25 libraries; aborting]))
AC_CHECK_LIB(ncurses, initscr, NCURSES_LIB="-lncurses", AC_MSG_ERROR([Could not find the ncurses libraries; aborting]))
AC_CHECK_LIB(m, cos, MATH_LIB="-lm", AC_MSG_ERROR([Could not find the math libraries; aborting]))

# Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([assert.h ctype.h errno.h malloc.h netdb.h paths.h signal.h stdlib.h string.h syslog.h termios.h unistd.h utmp.h sys/fcntl.h sys/file.h sys/io.h sys/ioctl.h sys/ipc.h sys/mman.h sys/param.h sys/socket.h sys/stat.h sys/time.h sys/un.h sys/utsname.h sys/types.h arpa/inet.h net/ethernet.h net/if.h net/if_arp.h netinet/in.h netinet/ip_icmp.h netinet/ip.h linux/if_ether.h])

# Checks for working glibc 2.1 headers
AC_MSG_CHECKING(for working netax25/ax25.h header file)
AC_EGREP_HEADER(ax25_fwd_struct, netax25/ax25.h, has_ax25=yes, has_ax25=no)
if test "$has_ax25" = "yes" ; then
  AC_DEFINE(HAVE_NETAX25_AX25_H, 1, [Define if you have a proper netax25/ax25.h header file])
  AC_MSG_RESULT(yes)
else
  AC_MSG_RESULT(no)
fi
AM_CONDITIONAL([AX25_H], [test "$has_ax25" = "yes"])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIGNAL
AC_STRUCT_TM
AC_HEADER_TIME

# Checks for library functions.
AC_FUNC_CHOWN
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_FORK
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_FUNC_SELECT_ARGTYPES
AC_FUNC_SETPGRP
AC_FUNC_STAT
AC_FUNC_VPRINTF
AC_CHECK_FUNCS(alarm dup2 gethostbyaddr gethostname gettimeofday inet_ntoa memchr memove memset mkdir munmap putenv select setenv socket sqrt strcasecmp strchar strcspn strdup strerror strncasecmp strpbrk strrchr strspn strstr strtol strtoul uname)


AC_CONFIG_FILES([Makefile \
	lib/Makefile \
	cfg/Makefile \
	man/Makefile \
	monitor/Makefile \
	node/Makefile \
	flex/Makefile \
	pad/Makefile \
	tcp/Makefile \
	wp/Makefile \
	tools/Makefile \
	stats/Makefile \
	doc/Makefile \
	doc/html/Makefile \
	help/Makefile \
	scripts/ax25parms \
	scripts/versions \
	scripts/fpac.sh:scripts/fpac.in \
	scripts/Makefile \
	etc/Makefile \
	fpac.spec])
AC_OUTPUT
