#!/bin/sh
#
# Log all environment variables to ZED_DEBUG_LOG.
#
# This can be a useful aid when developing/debugging ZEDLETs since it shows the
# environment variables defined for each zevent.

[ -f "${ZED_ZEDLET_DIR}/zed.rc" ] && . "${ZED_ZEDLET_DIR}/zed.rc"
. "${ZED_ZEDLET_DIR}/zed-functions.sh"

: "${ZED_DEBUG_LOG:="${TMPDIR:="/tmp"}/zed.debug.log"}"

zed_exit_if_ignoring_this_event

lockfile="$(basename -- "${ZED_DEBUG_LOG}").lock"

umask 077
zed_lock "${lockfile}"
exec >> "${ZED_DEBUG_LOG}"

printenv | sort
echo

exec >&-
zed_unlock "${lockfile}"
exit 0
