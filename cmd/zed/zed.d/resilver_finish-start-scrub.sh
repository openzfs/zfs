#!/bin/sh
# resilver_finish-start-scrub.sh
# Run a scrub after a resilver
#
# Exit codes:
# 1: Internal error
# 2: Script wasn't enabled in zed.rc
# 3: Scrubs are automatically started for sequential resilvers
[ -f "${ZED_ZEDLET_DIR}/zed.rc" ] && . "${ZED_ZEDLET_DIR}/zed.rc"
. "${ZED_ZEDLET_DIR}/zed-functions.sh"

[ "${ZED_SCRUB_AFTER_RESILVER}" = "1" ] || exit 2
[ "${ZEVENT_RESILVER_TYPE}" != "sequential" ] || exit 3
[ -n "${ZEVENT_POOL}" ] || exit 1
[ -n "${ZEVENT_SUBCLASS}" ] || exit 1
zed_check_cmd "${ZPOOL}" || exit 1

zed_log_msg "Starting scrub after resilver on ${ZEVENT_POOL}"
"${ZPOOL}" scrub "${ZEVENT_POOL}"
