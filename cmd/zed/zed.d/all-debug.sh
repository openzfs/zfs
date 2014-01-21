#!/bin/sh
#
# Log all environment variables to ZED_DEBUG_LOG.
#
test -f "${ZED_SCRIPT_DIR}/zed.rc" && . "${ZED_SCRIPT_DIR}/zed.rc"

# Override the default umask to restrict access to a newly-created logfile.
umask 077

# Append stdout to the logfile after obtaining an advisory lock.
exec >> "${ZED_DEBUG_LOG:=/tmp/zed.debug.log}"
flock -x 1

printenv | sort
echo

exit 0
