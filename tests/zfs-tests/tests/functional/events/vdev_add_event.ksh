#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

# DESCRIPTION:
# Verify the vdev_add event describes each top-level vdev added.
#
# STRATEGY:
# 1. Create a pool and clear the events.
# 2. Add two top-level vdevs with a single 'zpool add'.
# 3. Verify 'zpool events -v' has one vdev_add event per added vdev, with
#    the vdev_guid and vdev_path of that vdev.
# 4. Verify the ZED receives the same vdev guids and paths.
# 5. Verify a mirror added gives one event with its guid and no path.
# 6. Verify adding only a spare still generates a vdev_add event.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/events/events_common.kshlib

verify_runnable "both"

function cleanup
{
	poolexists $MPOOL && destroy_pool $MPOOL
	log_must zed_stop
	log_must rm -f $VDEV1 $VDEV2 $VDEV3 $VDEV4 $VDEV5 $VDEV6 $TMP_EVENTS \
	    $TMP_EVENTS.out $TMP_EVENTS.exp
}

# Convert the hexadecimal guid in front of each "<guid> <path>" line read to
# decimal, leaving "-" alone.
function guids_to_decimal
{
	while read guid path; do
		[[ $guid == "-" ]] || guid=$(printf "%u" $guid)
		echo "$guid $path"
	done
}

# Print "<guid> <path>" of each vdev_add event in the given 'zpool events -v'
# output, with the guid in decimal, or "- -" if the event has no vdev.
function vdev_add_events
{
	awk '
	    /sysevent\.fs\.zfs\.vdev_add$/ { inev = 1; guid = "-"; path = "-" }
	    inev && $1 == "vdev_guid" { guid = $3 }
	    inev && $1 == "vdev_path" { path = $3; gsub(/"/, "", path) }
	    inev && /^$/ { print guid, path; inev = 0 }
	    END { if (inev) print guid, path }
	' $1 | guids_to_decimal
}

# Print "<guid> <path>" of each vdev_add event the ZED logged.
function zed_vdev_add_vdevs
{
	awk 'BEGIN { FS = "\n"; RS = "" }
	    $0 ~ /\nZEVENT_CLASS=sysevent\.fs\.zfs\.vdev_add\n/ {
		guid = "-"; path = "-"
		for (i = 1; i <= NF; i++) {
			if (sub(/^ZEVENT_VDEV_GUID=/, "", $i))
				guid = $i
			else if (sub(/^ZEVENT_VDEV_PATH=/, "", $i))
				path = $i
		}
		print guid, path
	    }' $ZED_DEBUG_LOG | guids_to_decimal
}

VDEV5=$TEST_BASE_DIR/vdev5
VDEV6=$TEST_BASE_DIR/vdev6

log_assert "The vdev_add event describes each top-level vdev added"
log_onexit cleanup

log_must truncate -s $MINVDEVSIZE $VDEV1 $VDEV2 $VDEV3 $VDEV4 $VDEV5 $VDEV6
log_must zpool create -m none $MPOOL $VDEV1
log_must zed_start
log_must zpool events -c
log_must truncate -s 0 $ZED_DEBUG_LOG

log_must zpool add $MPOOL $VDEV2 $VDEV3

guid2=$(zpool get -H -p -o value guid $MPOOL $VDEV2)
guid3=$(zpool get -H -p -o value guid $MPOOL $VDEV3)

log_must eval "zpool events -v $MPOOL > $TMP_EVENTS"
vdev_add_events $TMP_EVENTS | sort > $TMP_EVENTS.out
log_note "vdev_add events:" "$(cat $TMP_EVENTS.out)"
printf "%s %s\n%s %s\n" $guid2 $VDEV2 $guid3 $VDEV3 | sort > $TMP_EVENTS.exp
log_must diff $TMP_EVENTS.out $TMP_EVENTS.exp

# The ZED gets the same guids and paths.
log_must file_wait_event $ZED_DEBUG_LOG 'sysevent\.fs\.zfs\.vdev_add' 150
for vdev in "$guid2 $VDEV2" "$guid3 $VDEV3"; do
	typeset -i i=0
	while ! zed_vdev_add_vdevs | grep -qx "$vdev"; do
		(( i++ < 60 )) || log_fail "ZED did not see vdev_add for $vdev"
		sleep 1
	done
done

# A top-level mirror has a guid but no path.
log_must zpool events -c
log_must zpool add -f $MPOOL mirror $VDEV5 $VDEV6
mirror_guid=$(zpool get -H -p -o value guid $MPOOL mirror-3)
log_must eval "zpool events -v $MPOOL > $TMP_EVENTS"
[[ "$(vdev_add_events $TMP_EVENTS)" == "$mirror_guid -" ]] ||
    log_fail "unexpected mirror vdev_add event: $(vdev_add_events $TMP_EVENTS)"

# An event is still generated when no top-level vdev is added.
log_must zpool events -c
log_must zpool add $MPOOL spare $VDEV4
log_must eval "zpool events -v $MPOOL > $TMP_EVENTS"
typeset -i nevents=$(vdev_add_events $TMP_EVENTS | wc -l)
(( nevents == 1 )) || log_fail "expected one vdev_add event, got $nevents"

log_pass "The vdev_add event describes each top-level vdev added"
