#!/bin/sh
#
# Replace a device with a hot spare in response to IO or CHECKSUM errors.
# The following actions will be performed automatically when the number
# of errors exceed the limit set by ZED_SPARE_ON_IO_ERRORS or
# ZED_SPARE_ON_CHECKSUM_ERRORS.
#
# 1) FAULT the device on IO errors, no futher IO will be attempted.
#    DEGRADE the device on checksum errors, the device is still
#    functional and can be used to service IO requests.
# 2) Set the SES fault beacon for the device.
# 3) Replace the device with a hot spare if any are available.
#
# Once the hot sparing operation is complete either the failed device or
# the hot spare must be manually retired using the 'zpool detach' command.
# The 'autoreplace' functionality which would normally take care of this
# under Illumos has not yet been implemented.
#
# Full support for autoreplace is planned, but it requires that the full
# ZFS Diagnosis Engine be ported.  In the meanwhile this script provides
# the majority of the expected hot spare functionality.
#
# Exit codes:
#   0: hot spare replacement successful
#   1: hot spare device not available
#   2: hot sparing disabled or threshold not reached
#   3: device already faulted or degraded
#   9: internal error

[ -f "${ZED_ZEDLET_DIR}/zed.rc" ] && . "${ZED_ZEDLET_DIR}/zed.rc"
. "${ZED_ZEDLET_DIR}/zed-functions.sh"

# Disabled by default.  Enable in the zed.rc file.
: "${ZED_SPARE_ON_CHECKSUM_ERRORS:=0}"
: "${ZED_SPARE_ON_IO_ERRORS:=0}"


# query_vdev_status (pool, vdev)
#
# Given a [pool] and [vdev], return the matching vdev path & status on stdout.
#
# Warning: This function does not handle the case of [pool] or [vdev]
# containing whitespace.  Beware of ShellCheck SC2046.  Caveat emptor.
#
# Arguments
#   pool: pool name
#   vdev: virtual device name
#
# StdOut
#   arg1: vdev pathname
#   arg2: vdev status
#
query_vdev_status()
{
    local pool="$1"
    local vdev="$2"
    local t

    vdev="$(basename -- "${vdev}")"
    ([ -n "${pool}" ] && [ -n "${vdev}" ]) || return
    t="$(printf '\t')"

    "${ZPOOL}" status "${pool}" 2>/dev/null | sed -n -e \
        "s,^[ $t]*\(.*${vdev}\(-part[0-9]\+\)\?\)[ $t]*\([A-Z]\+\).*,\1 \3,p" \
        | tail -1
}


# notify (old_vdev, new_vdev, num_errors)
#
# Send a notification regarding the hot spare replacement.
#
# Arguments
#   old_vdev: path of old vdev that has failed
#   new_vdev: path of new vdev used as the hot spare replacement
#   num_errors: number of errors that triggered this replacement
#
notify()
{
    local old_vdev="$1"
    local new_vdev="$2"
    local num_errors="$3"
    local note_subject
    local note_pathname
    local s
    local rv

    umask 077
    note_subject="ZFS hot spare replacement for ${ZEVENT_POOL} on $(hostname)"
    note_pathname="${TMPDIR:="/tmp"}/$(basename -- "$0").${ZEVENT_EID}.$$"
    {
        [ "${num_errors}" -ne 1 ] 2>/dev/null && s="s"

        echo "ZFS has replaced a failing device with a hot spare after" \
            "${num_errors} ${ZEVENT_SUBCLASS} error${s}:"
        echo
        echo "   eid: ${ZEVENT_EID}"
        echo " class: ${ZEVENT_SUBCLASS}"
        echo "  host: $(hostname)"
        echo "  time: ${ZEVENT_TIME_STRING}"
        echo "   old: ${old_vdev}"
        echo "   new: ${new_vdev}"

        "${ZPOOL}" status "${ZEVENT_POOL}"

    } > "${note_pathname}"

    zed_notify "${note_subject}" "${note_pathname}"; rv=$?
    rm -f "${note_pathname}"
    return "${rv}"
}


# main
#
# Arguments
#   none
#
# Return
#   see above
#
main()
{
    local num_errors
    local action
    local lockfile
    local vdev_path
    local vdev_status
    local spare
    local spare_path
    local spare_status
    local zpool_err
    local zpool_rv
    local rv

    # Avoid hot-sparing a hot-spare.
    #
    # Note: ZEVENT_VDEV_PATH is not defined for ZEVENT_VDEV_TYPE=spare.
    #
    [ "${ZEVENT_VDEV_TYPE}" = "spare" ] && exit 2

    [ -n "${ZEVENT_POOL}" ] || exit 9
    [ -n "${ZEVENT_VDEV_GUID}" ] || exit 9
    [ -n "${ZEVENT_VDEV_PATH}" ] || exit 9

    zed_check_cmd "${ZPOOL}" "${ZINJECT}" || exit 9

    # Fault the device after a given number of I/O errors.
    #
    if [ "${ZEVENT_SUBCLASS}" = "io" ]; then
        if [ "${ZED_SPARE_ON_IO_ERRORS}" -gt 0 ]; then
            num_errors=$((ZEVENT_VDEV_READ_ERRORS + ZEVENT_VDEV_WRITE_ERRORS))
            [ "${num_errors}" -ge "${ZED_SPARE_ON_IO_ERRORS}" ] \
                && action="fault"
        fi 2>/dev/null

    # Degrade the device after a given number of checksum errors.
    #
    elif [ "${ZEVENT_SUBCLASS}" = "checksum" ]; then
        if [ "${ZED_SPARE_ON_CHECKSUM_ERRORS}" -gt 0 ]; then
            num_errors="${ZEVENT_VDEV_CKSUM_ERRORS}"
            [ "${num_errors}" -ge "${ZED_SPARE_ON_CHECKSUM_ERRORS}" ] \
                && action="degrade"
        fi 2>/dev/null

    else
        zed_log_err "unsupported event class \"${ZEVENT_SUBCLASS}\""
        exit 9
    fi

    # Error threshold not reached.
    #
    if [ -z "${action}" ]; then
        exit 2
    fi

    lockfile="zed.spare.lock"
    zed_lock "${lockfile}"

    # shellcheck disable=SC2046
    set -- $(query_vdev_status "${ZEVENT_POOL}" "${ZEVENT_VDEV_PATH}")
    vdev_path="$1"
    vdev_status="$2"

    # Device is already FAULTED or DEGRADED.
    #
    if [ "${vdev_status}" = "FAULTED" ] \
            || [ "${vdev_status}" = "DEGRADED" ]; then
        rv=3

    else
        rv=1

        # 1) FAULT or DEGRADE the device.
        #
        "${ZINJECT}" -d "${ZEVENT_VDEV_GUID}" -A "${action}" "${ZEVENT_POOL}"

        # 2) Set the SES fault beacon.
        #
        # TODO: Set the 'fault' or 'ident' beacon for the device.  This can
        # be done through the sg_ses utility.  The only hard part is to map
        # the sd device to its corresponding enclosure and slot.  We may
        # be able to leverage the existing vdev_id scripts for this.
        #
        # $ sg_ses --dev-slot-num=0 --set=ident /dev/sg3
        # $ sg_ses --dev-slot-num=0 --clear=ident /dev/sg3

        # 3) Replace the device with a hot spare.
        #
        # Round-robin through the spares trying those that are available.
        #
        for spare in ${ZEVENT_VDEV_SPARE_PATHS}; do

            # shellcheck disable=SC2046
            set -- $(query_vdev_status "${ZEVENT_POOL}" "${spare}")
            spare_path="$1"
            spare_status="$2"

            [ "${spare_status}" = "AVAIL" ] || continue

            zpool_err="$("${ZPOOL}" replace "${ZEVENT_POOL}" \
                "${ZEVENT_VDEV_GUID}" "${spare_path}" 2>&1)"; zpool_rv=$?

            if [ "${zpool_rv}" -ne 0 ]; then
                [ -n "${zpool_err}" ] && zed_log_err "zpool ${zpool_err}"
            else
                notify "${vdev_path}" "${spare_path}" "${num_errors}"
                rv=0
                break
            fi
        done
    fi

    zed_unlock "${lockfile}"
    exit "${rv}"
}


main "$@"
