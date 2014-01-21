#!/bin/sh
#
# Log all environment variables to ZED_DEBUG_LOG.
#
test -f zed.rc && . ./zed.rc

umask 077
exec >> "${ZED_DEBUG_LOG:=/tmp/zed.debug.log}"
flock -x 1
printenv | sort
echo
exit 0
