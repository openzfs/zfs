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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#


#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/snapdir/snapdir.cfg
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

if ! is_linux ; then
	log_unsupported "ADMIN_SNAPSHOT tunable not available on this platform."
fi

save_tunable ADMIN_SNAPSHOT

function cleanup
{
	typeset -i i=0
	while ((i < snap_cnt)); do
		typeset snap=$fs@snap.$i
		datasetexists $snap && destroy_dataset $snap -f

		((i += 1))
	done

	restore_tunable ADMIN_SNAPSHOT
}

zfs 2>&1 | grep "allow" > /dev/null
(($? != 0)) && log_unsupported

log_assert "Verify snapshot can be created via mkdir in .zfs/snapshot."
log_onexit cleanup

log_must set_tunable64 ADMIN_SNAPSHOT 1

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
	log_must makedir $snapdir/snapshot/snap.$cnt
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
