#! /bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/snapshot/snapshot.cfg

#
# DESCRIPTION:
# Verify that destroying snapshots returns space to the pool.
#
# STRATEGY:
# 1. Create a file system and populate it while snapshotting.
# 2. Destroy the snapshots and remove the files.
# 3. Verify the space returns to the pool.
#

verify_runnable "both"

function cleanup
{
	typeset -i i=1
	while [[ $i -lt $COUNT ]]; do
		snapexists $SNAPFS.$i &&
			log_must zfs destroy $SNAPFS.$i

		(( i = i + 1 ))
	done

	[ -e $TESTDIR ] && log_must rm -rf $TESTDIR/*
}

log_assert "Verify that destroying snapshots returns space to the pool."

log_onexit cleanup

[ -n $TESTDIR ] && log_must rm -rf $TESTDIR/*

typeset -i COUNT=10

orig_size=`get_prop available $TESTPOOL`

log_note "Populate the $TESTDIR directory"
typeset -i i=1
while [[ $i -lt $COUNT ]]; do
	log_must file_write -o create -f $TESTDIR/file$i \
	   -b $BLOCKSZ -c $NUM_WRITES -d $i

	log_must zfs snapshot $SNAPFS.$i
	(( i = i + 1 ))
done

typeset -i i=1
while [[ $i -lt $COUNT ]]; do
	log_must rm -f $TESTDIR/file$i
	log_must zfs destroy $SNAPFS.$i

	(( i = i + 1 ))
done

wait_freeing $TESTPOOL
sync_pool

new_size=`get_prop available $TESTPOOL`

typeset -i tolerance=0

(( tolerance = new_size - orig_size))
if (( tolerance > LIMIT )); then
        log_fail "Space not freed. ($orig_size != $new_size)"
fi

log_pass "After destroying snapshots, the space is returned to the pool."
