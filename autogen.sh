#!/bin/sh

aclocal -I config &&
libtoolize --automake --copy
autoheader &&
automake --add-missing --include-deps 2>/dev/null
autoconf
rm -rf autom4te.cache aclocal.m4
