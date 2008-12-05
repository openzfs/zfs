#!/bin/sh

aclocal -I config &&
libtoolize --automake --copy
autoheader &&
automake --add-missing --include-deps
autoconf
rm -rf autom4te.cache aclocal.m4
