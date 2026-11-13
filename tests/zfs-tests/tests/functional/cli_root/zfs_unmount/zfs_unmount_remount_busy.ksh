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
#	'zfs unmount' must not fail with "busy" just because the file system
#	is busy for a moment.  On FreeBSD, mountd(8) applies every export
#	change with nmount(MNT_UPDATE) on the exported file systems, and
#	unmount(2) returns EBUSY while such an update is in progress.
#	'zfs unmount' of a dataset with sharenfs=on signals mountd just
#	before its own unmount(2), so 'zfs set canmount=off', 'zfs unmount'
#	or 'zfs destroy' of a shared dataset failed now and then
#	(inheritance/inherit_001_pos).  libzfs retries an unmount that fails
#	with EBUSY for a short while (about 1.3 s).  A file system that stays
#	in use must still refuse to unmount.
#	FreeBSD only: exportfs(8) on Linux doesn't update the mount.
#
# STRATEGY:
#	1. Hold the file system busy (a process with its working directory
#	   in it) and run 'zfs unmount'.  Release the file system only once
#	   'zfs unmount' has had EBUSY and is waiting to try again (asleep in
#	   nanosleep), or has exited.  It must succeed.  Without the retry it
#	   fails at once, every time; nothing depends on timing.
#	2. Smoke test of the real case: unmount and mount the file system 50
#	   times while a background loop updates its mount (mount -u), as
#	   mountd does.  Every 'zfs unmount' must succeed.  (An update can't
#	   be held open on purpose: whatever stalls ZFS's part of it also
#	   stalls unmount(2)'s lookup of the mount point, which then waits
#	   instead of failing.)
#	3. Hold the file system busy for good: 'zfs unmount' must fail with
#	   "busy" once the retries run out, and leave it mounted.
#	4. Release it: 'zfs unmount' must now succeed, and 'zfs mount' bring
#	   it back.
#

verify_runnable "both"

if ! is_freebsd; then
	log_unsupported "FreeBSD-specific: mountd updates exported mounts"
fi

typeset fs=$TESTPOOL/$TESTFS1
# Under the pool root: when the file system is unmounted,
# "mount -u <dir>" updates the pool root mount instead, which is harmless.
typeset mntpnt=/$TESTPOOL/$TESTFS1
typeset ready=$TEST_BASE_DIR/remount_busy.ready
typeset runflag=$TEST_BASE_DIR/remount_busy.run
typeset outfile=$TEST_BASE_DIR/remount_busy.out
typeset -i cycles=50
typeset -i storm_pid=0 holder_pid=0 unmount_pid=0

# Start a process whose working directory is in the file system.
function hold_busy
{
	rm -f $ready
	(cd $mntpnt && touch $ready && exec sleep 300) &
	holder_pid=$!
	while [[ ! -e $ready ]]; do
		kill -0 $holder_pid 2>/dev/null || \
		    log_fail "could not start a process in $mntpnt"
		sleep 0.01
	done
}

function release
{
	kill $holder_pid
	wait $holder_pid
	holder_pid=0
}

function remount_storm
{
	while [[ -e $runflag ]]; do
		mount -u -o noatime $mntpnt
		mount -u -o atime $mntpnt
	done >/dev/null 2>&1
}

function cleanup
{
	rm -f $runflag $ready $outfile
	(( storm_pid != 0 )) && wait $storm_pid
	(( holder_pid != 0 )) && kill $holder_pid 2>/dev/null
	(( unmount_pid != 0 )) && wait $unmount_pid
	datasetexists $fs && destroy_dataset $fs -f
}

log_onexit cleanup

log_assert "'zfs unmount' retries a file system that is busy for a moment" \
	"and still fails with 'busy' when it stays in use."

log_must zfs create $fs
log_must test "$(get_prop mountpoint $fs)" = $mntpnt
log_must ismounted $fs

# 1. busy until 'zfs unmount' has failed once and waits to retry
hold_busy
zfs unmount $fs >$outfile 2>&1 &
unmount_pid=$!
typeset -i polls=0
while (( polls < 2000 )); do
	typeset wchan=$(ps -o wchan= -p $unmount_pid 2>/dev/null)
	[[ -z $wchan || $wchan == nanslp ]] && break
	sleep 0.005
	(( polls += 1 ))
done
log_note "zfs unmount: wait channel '$wchan' after $polls polls"
release
wait $unmount_pid
typeset -i rc=$?
unmount_pid=0
if (( rc != 0 )); then
	log_fail "zfs unmount $fs failed although it was busy only until" \
	    "the first attempt: $(<$outfile)"
fi
log_mustnot ismounted $fs
log_must zfs mount $fs

# 2. unmount/mount cycles while the mount is being updated
log_must touch $runflag
remount_storm &
storm_pid=$!
typeset -i i=0
while (( i < cycles )); do
	out=$(zfs unmount $fs 2>&1)
	if (( $? != 0 )); then
		log_fail "zfs unmount $fs failed during a remount" \
		    "(cycle $i of $cycles): $out"
	fi
	if ismounted $fs; then
		log_fail "$fs still mounted after zfs unmount (cycle $i)"
	fi
	log_must zfs mount $fs
	(( i += 1 ))
done
log_note "$cycles unmount/mount cycles succeeded during remounts"
log_must rm -f $runflag
wait $storm_pid
storm_pid=0
log_must ismounted $fs

# 3. a file system that stays in use must still refuse
hold_busy
out=$(zfs unmount $fs 2>&1)
(( $? == 0 )) && log_fail "zfs unmount $fs succeeded while it was in use"
log_note "in use: $out"
[[ $out == *"busy"* ]] || log_fail "unexpected error for a busy unmount: $out"
log_must ismounted $fs

# 4. released: unmount and mount work again
release
log_must zfs unmount $fs
log_mustnot ismounted $fs
log_must zfs mount $fs
log_must ismounted $fs

log_pass "'zfs unmount' retries a file system that is busy for a moment" \
	"and still fails with 'busy' when it stays in use."
