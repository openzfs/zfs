#!/bin/sh

aclocal -I config
libtoolize --automake --copy
autoheader
automake --add-missing --include-deps --copy
autoconf
