#!/bin/sh
# VE7FET location
LIBAX25DIR=/usr/local/lib/
# distro location
LIBAXDIR=/usr/lib/
# AX.25 libraries & links
LIBAX25SO=libax25.so
LIBAX25IO=libax25io.so
VER=.1.0.0
DIR=$PWD
# Check AX.25 libraries directory location
if [ -f $LIBAXDIR/$LIBAX25SO$VER ] ; then
 echo "AX.25 libraries are in $LIBAXDIR"

# If required links do not exist lets create
 cd $LIBAXDIR
 if [ ! -f $LIBAX25SO ] ; then
  ln -s $LIBAX25SO$VER $LIBAX25SO
 echo "Created missing AX.25 library link $LIBAX25SO"
 fi
 if [ ! -f $LIBAXDIR/$LIBAX25IO ] ; then
  ln -s $LIBAX25IO$VER $LIBAX25IO
 echo "Created missing AX.25 library link $LIBAX25IO"
 fi

else
 echo "Looking if AX.25 libraries are in $LIBAX25DIR" 
 if [ -f $LIBAX25DIR/$LIBAX25SO$VER ] ; then
 echo "OK !"
 else
 echo "AX.25 libraries are not installed in this system !" 
 fi
fi

cd $DIR

echo "---removing generated files---"
# products from ./configure and make
if test -f Makefile ; then
# touch all automatically generated targets that we do not want to rebuild
touch aclocal.m4
touch configure
touch Makefile.in
touch config.status
touch Makefile
# make maintainer-clean
make -k maintainer-clean
fi

# products from autoreconf
rm -Rf autom4te.cache
rm -f ./m4/*
rm -f Makefile.in aclocal.m4 compile config.guess config.sub configure \
      config.h.in depcomp install-sh ltmain.sh missing build-arch-stamp \
      ar-lib 
rm -f `find . -name "Makefile.in"`


if test "$1" != "clean" ; then
  # bootstrap
  echo "---autoreconf---"
  autoreconf -v -i -f
fi

