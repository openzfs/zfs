#!/bin/ksh -p
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#


#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/snapshot/snapshot.cfg
. $STF_SUITE/tests/functional/cli_root/zfs_rollback/zfs_rollback_common.kshlib

#
# DESCRIPTION:
#	Verify snapshot can be created or destroy via mkdir or rm
#	in .zfs/snapshot.
#
# STRATEGY:
#	1. Verify make directories only successfully in .zfs/snapshot.
#	2. Verify snapshot can be created and destroy via mkdir and remove
#	directories in .zfs/snapshot.
#	3. Verify rollback to previous snapshot can succeed.
#	4. Verify remove directory in snapdir can destroy snapshot.
#

verify_runnable "both"

function cleanup
{
	typeset -i i=0
	while ((i < snap_cnt)); do
		typeset snap=$fs@snap.$i
		datasetexists $snap && destroy_dataset $snap -f

		((i += 1))
	done
}

zfs 2>&1 | grep "allow" > /dev/null
(($? != 0)) && log_unsupported

log_assert "Verify snapshot can be created via mkdir in .zfs/snapshot."
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
# Verify all the other directories are readonly.
mntpnt=$(get_prop mountpoint $fs)
snapdir=$mntpnt/.zfs
set -A ro_dirs "$snapdir" "$snapdir/snap" "$snapdir/snapshot"
for dir in ${ro_dirs[@]}; do
	if [[ -d $dir ]]; then
		log_mustnot rm -rf $dir
		log_mustnot touch $dir/testfile
	else
		log_mustnot mkdir $dir
	fi
done

# Verify snapshot can be created via mkdir in .zfs/snapshot
typeset -i snap_cnt=5
typeset -i cnt=0
while ((cnt < snap_cnt)); do
	testfile=$mntpnt/testfile.$cnt
	log_must mkfile 1M $testfile
	log_must mkdir $snapdir/snapshot/snap.$cnt
	if ! datasetexists $fs@snap.$cnt ; then
		log_fail "ERROR: $fs@snap.$cnt should exists."
	fi

	((cnt += 1))
done

# Verify rollback to previous snapshot succeed.
((cnt = RANDOM % snap_cnt))
log_must zfs rollback -r $fs@snap.$cnt

typeset -i i=0
while ((i < snap_cnt)); do
	testfile=$mntpnt/testfile.$i
	if ((i <= cnt)); then
		if [[ ! -f $testfile ]]; then
			log_fail "ERROR: $testfile should exists."
		fi
	else
		if [[ -f $testfile ]]; then
			log_fail "ERROR: $testfile should not exists."
		fi
	fi

	((i += 1))
done

# Verify remove directory in snapdir can destroy snapshot.
log_must rmdir $snapdir/snapshot/snap.$cnt
log_mustnot datasetexists $fs@snap.$cnt

log_pass "Verify snapshot can be created via mkdir in .zfs/snapshot passed."
