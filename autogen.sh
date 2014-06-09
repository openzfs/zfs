#!/bin/sh

autoreconf -fiv $(dirname $0)/configure.ac
rm -Rf $(dirname $0)/autom4te.cache
