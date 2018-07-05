#!/bin/sh

autoreconf -fiv || exit 1
rm -Rf autom4te.cache
