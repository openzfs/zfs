#!/bin/sh

find . -type d -name .deps | xargs rm -rf
aclocal 2>/dev/null &&
autoheader &&
automake --add-missing --include-deps # 2>/dev/null &&
autoconf

