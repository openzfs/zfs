#!/bin/sh

autoreconf -fiv "$(dirname "$0")" && rm -rf "$(dirname "$0")"/autom4te.cache
