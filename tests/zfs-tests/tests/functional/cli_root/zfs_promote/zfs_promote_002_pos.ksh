#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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

