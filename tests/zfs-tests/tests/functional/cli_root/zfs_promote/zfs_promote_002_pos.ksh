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

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zfs_promote/zfs_promote.cfg
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zfs promote' can deal with multiple snapshots in the origin filesystem.
#
# STRATEGY:
#	1. Create multiple snapshots and a clone of the last snapshot
#	2. Promote the clone filesystem
#	3. Verify the promoted filesystem included all snapshots
#

verify_runnable "both"

function cleanup
{
	if snapexists $csnap1; then
		log_must zfs promote $fs
	fi

	typeset ds
	typeset data
	for ds in $snap $snap1; do
		log_must zfs destroy -rR $ds
	done
	for file in $TESTDIR/$TESTFILE0 $TESTDIR/$TESTFILE1; do
		[[ -e $file ]] && rm -f $file
	done
}

log_assert "'zfs promote' can deal with multiple snapshots in a filesystem."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
snap=$fs@$TESTSNAP
snap1=$fs@$TESTSNAP1
clone=$TESTPOOL/$TESTCLONE
csnap=$clone@$TESTSNAP
csnap1=$clone@$TESTSNAP1

# setup for promote testing
log_must mkfile $FILESIZE $TESTDIR/$TESTFILE0
log_must zfs snapshot $snap
log_must mkfile $FILESIZE $TESTDIR/$TESTFILE1
log_must rm -f $testdir/$TESTFILE0
log_must zfs snapshot $snap1
log_must zfs clone $snap1 $clone
log_must mkfile $FILESIZE /$clone/$CLONEFILE

log_must zfs promote $clone

# verify the 'promote' operation
for ds in $csnap $csnap1; do
	! snapexists $ds && \
		log_fail "Snapshot $ds doesn't exist after zfs promote."
done
for ds in $snap $snap1; do
	snapexists $ds && \
		log_fail "Snapshot $ds is still there after zfs promote."
done

origin_prop=$(get_prop origin $fs)
[[ "$origin_prop" != "$csnap1" ]] && \
	log_fail "The dependency of $fs is not correct."
origin_prop=$(get_prop origin $clone)
[[ "$origin_prop" != "-" ]] && \
	 log_fail "The dependency of $clone is not correct."

log_pass "'zfs promote' deal with multiple snapshots as expected."

