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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# On a pool without resilver_defer, a scan stops before the txg it was set
# up in, and a resilver of what is left follows the scan:
# - a resilver started by an attach completes in its first pass while
#   writes to the other child keep failing, instead of restarting;
# - a scrub set up in a txg with a failed write is followed by a resilver
#   that leaves no DTL.
#
# STRATEGY:
# 1. Create a mirror pool with feature@resilver_defer=disabled.
# 2. Keep writing, fail writes to one child, and attach a third child.
#    Check that its resilver completes in its first pass while the writes
#    still fail.
# 3. Recreate the pool.  Keep writing and fail writes to one child while
#    "zpool scrub" sets up the scrub.
# 4. Wait until no scan runs, export the pool and check with zdb that no
#    vdev has a DTL left.
#

verify_runnable "global"

function cleanup
{
	stop_writer
	zinject -c all >/dev/null 2>&1
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -rf $VDEV_DIR
}

function start_writer
{
	typeset mnt=$(get_prop mountpoint $TESTPOOL1)

	while true; do
		dd if=/dev/urandom of=$mnt/file bs=1M count=16 \
		    conv=notrunc status=none
	done &
	writer_pid=$!
}

function stop_writer
{
	[[ -n "$writer_pid" ]] && kill $writer_pid 2>/dev/null
	wait
	writer_pid=""
}

function create_pool
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	# Fresh files: an old label must not reach "zdb -e" or the import.
	log_must rm -f $VDEV1 $VDEV2 $VDEV3
	log_must truncate -s $MINVDEVSIZE $VDEV1 $VDEV2 $VDEV3
	log_must zpool create -f -o feature@resilver_defer=disabled \
	    $TESTPOOL1 mirror $VDEV1 $VDEV2
	log_must test "$(get_pool_prop feature@resilver_defer $TESTPOOL1)" \
	    == "disabled"
	log_must zfs set compression=off $TESTPOOL1
}

function scans_done
{
	zpool history -i $TESTPOOL1 | grep -c "scan done"
}

# Succeed when no scrub or resilver has run for 3 s in a row.
function wait_scans_idle
{
	typeset -i i=0 idle=0

	while (( i < 120 && idle < 3 )); do
		if zpool status $TESTPOOL1 | grep -q "in progress"; then
			idle=0
		else
			idle=$((idle + 1))
		fi
		i=$((i + 1))
		sleep 1
	done
	(( idle >= 3 ))
}

log_assert "Without resilver_defer, a scan is followed by a resilver of" \
	"what it left, and an attach's resilver completes"
log_onexit cleanup

VDEV_DIR=$TEST_BASE_DIR/scrub_defer_off
log_must mkdir -p $VDEV_DIR
VDEV1=$VDEV_DIR/vdev1
VDEV2=$VDEV_DIR/vdev2
VDEV3=$VDEV_DIR/vdev3

# An attach while writes to the other child keep failing.
create_pool
start_writer
sleep 4
typeset -i before=$(scans_done)
log_must zinject -d $VDEV2 -e io -T write $TESTPOOL1
log_must zpool attach $TESTPOOL1 $VDEV1 $VDEV3
typeset -i i=0
while (( i < 60 && $(scans_done) == before )); do
	i=$((i + 1))
	sleep 1
done
history=$(zpool history -i $TESTPOOL1 | grep -E "scan (setup|aborted|done)")
log_note "$history"
(( $(scans_done) > before )) ||
    log_fail "no scan completed in 60 s while writes kept failing"
# The attach's resilver completes in its first pass: it is not restarted.
next=$(echo "$history" | awk '/scan setup func=2 /{ found = 1; next }
	found { print; exit }')
[[ "$next" == *"scan done"* ]] ||
    log_fail "the attach's resilver did not complete in its first pass: $next"
log_must zinject -c all
stop_writer
log_must wait_scans_idle

# A scrub set up in a txg with a failed write.
create_pool
start_writer
sleep 4
log_must zinject -d $VDEV2 -e io -T write $TESTPOOL1
log_must zpool scrub $TESTPOOL1
log_must zinject -c all
stop_writer
log_must wait_scans_idle
log_must zpool status -v $TESTPOOL1
log_note "$(zpool history -i $TESTPOOL1 | grep -E "scan (setup|aborted|done)")"
log_must zpool export $TESTPOOL1
dtl=$(zdb -e -p $VDEV_DIR -ddd $TESTPOOL1 | grep -E "missing +\[")
log_must zpool import -d $VDEV_DIR $TESTPOOL1
[[ -z "$dtl" ]] || log_fail "a DTL is left after the scans: $dtl"

log_pass "Without resilver_defer, a scan is followed by a resilver of" \
	"what it left, and an attach's resilver completes"
