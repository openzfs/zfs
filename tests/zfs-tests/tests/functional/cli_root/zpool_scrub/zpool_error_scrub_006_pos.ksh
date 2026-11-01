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
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_error_scrub.kshlib

#
# DESCRIPTION:
#	Verify an error scrub that was in progress (or paused) when the pool
#	was exported resumes from where it was after import, and scrubs the
#	error blocks rather than dropping the errors unverified.
#
# STRATEGY:
#	For an in-progress and for a paused error scrub:
#	1. Create a pool and a file in it, and create permanent errors
#	   for the file.
#	2. Suspend scan progress, start an error scrub, and optionally
#	   pause it.
#	3. Export and import the pool.
#	4. Verify the error scrub has not finished and the errors are
#	   still reported.
#	5. Inject checksum errors again, so the corruption persists.
#	6. Resume the error scrub if paused, and resume scan progress.
#	7. Wait for the error scrub to finish, and verify it examined the
#	   error blocks and still reports the errors.
#

verify_runnable "global"

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	zinject -c all
	destroy_pool $TESTPOOL2
	rm -f $TESTDIR/vdev_a
}

log_onexit cleanup

log_assert "Verify an error scrub resumes correctly after pool import."

typeset file=/$TESTPOOL2/$TESTFS1/$TESTFILE0

for mode in active paused; do
	log_note "Testing export/import with $mode error scrub"

	truncate -s $MINVDEVSIZE $TESTDIR/vdev_a
	log_must zpool create -f -O primarycache=none $TESTPOOL2 \
	    $TESTDIR/vdev_a
	log_must zfs create -o compression=off $TESTPOOL2/$TESTFS1
	log_must dd if=/dev/urandom of=$file bs=1M count=4
	log_must sync_pool $TESTPOOL2

	create_permanent_errors $TESTPOOL2 $file

	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
	log_must zpool scrub -e $TESTPOOL2
	log_must is_pool_error_scrubbing $TESTPOOL2 true
	if [[ $mode == paused ]]; then
		log_must zpool scrub -p $TESTPOOL2
		log_must is_pool_error_scrub_paused $TESTPOOL2 true
	fi

	export_with_deadline $TESTPOOL2
	log_must zpool import -d $TESTDIR $TESTPOOL2

	log_must zpool status -v $TESTPOOL2
	log_mustnot is_pool_error_scrubbed $TESTPOOL2
	log_mustnot is_pool_error_scrub_stopped $TESTPOOL2
	log_must eval "zpool status -v $TESTPOOL2 | grep '$file'"

	# The corruption persists, so the error scrub must keep the errors.
	log_must zinject -t data -e checksum -f 100 -am $file

	if [[ $mode == paused ]]; then
		log_must zpool scrub -e $TESTPOOL2
	fi
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	wait_error_scrubbed $TESTPOOL2
	log_must zpool status -v $TESTPOOL2
	log_mustnot eval "zpool status -v $TESTPOOL2 | \
	    grep 'scrubbed 0 error blocks'"
	log_must eval "zpool status -v $TESTPOOL2 | \
	    grep 'Permanent errors have been detected'"
	log_must eval "zpool status -v $TESTPOOL2 | grep '$file'"

	log_must zinject -c all
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	destroy_pool $TESTPOOL2
	rm -f $TESTDIR/vdev_a
done

log_pass "Verified an error scrub resumes correctly after pool import."
