#!/bin/sh
# shellcheck disable=SC2154
#
# Log all environment variables to ZED_DEBUG_LOG.
#
# This can be a useful aid when developing/debugging ZEDLETs since it shows the
# environment variables defined for each zevent.

[ -f "${ZED_ZEDLET_DIR}/zed.rc" ] && . "${ZED_ZEDLET_DIR}/zed.rc"
. "${ZED_ZEDLET_DIR}/zed-functions.sh"

: "${ZED_DEBUG_LOG:="${TMPDIR:="/tmp"}/zed.debug.log"}"

zed_exit_if_ignoring_this_event

zed_lock "${ZED_DEBUG_LOG}"
{
	env | sort
	echo
} 1>&"${ZED_FLOCK_FD}"
zed_unlock "${ZED_DEBUG_LOG}"

exit 0
