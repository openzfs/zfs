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
# Copyright (c) 2016, 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/channel_program/channel_common.kshlib

#
# DESCRIPTION: Check various invalid snapshot error cases
#
# zfs.sync.snapshot() and zfs.sync.rename_snapshot() must reject the snapshot
# names that zfs snapshot and zfs rename reject.  zfs.check.* must return the
# same error, and zfs.sync.* must still fail in open context (zfs program -n),
# as it does for any other argument.
#

verify_runnable "global"

fs1=$TESTPOOL/$TESTFS/testchild1
fs2=$TESTPOOL/$TESTFS/testchild2

function cleanup
{
	for fs in $fs1 $fs2; do
		destroy_dataset $fs "-R"
	done
}

log_onexit cleanup

log_must zfs create $fs1
log_must zfs create $fs2
log_must zfs snapshot $fs1@snap1

log_must_program_sync $TESTPOOL \
    $ZCP_ROOT/synctask_core/tst.snapshot_neg.zcp $fs1 $fs2

#
# Each call must return the error from zfs.sync.* and zfs.check.* in syncing
# context, and zfs.sync.* must fail with "requires passing sync=TRUE" in open
# context.
#
function check_neg # error call
{
	log_must_program_sync $TESTPOOL - <<-EOF
		assert(zfs.sync.$2 == $1)
		assert(zfs.check.$2 == $1)
	EOF
}

# empty or invalid snapshot names
check_neg EINVAL "snapshot('$fs1@')"
check_neg EINVAL "snapshot('$fs1@bad%name')"
check_neg EINVAL "snapshot('$fs1@bad*name')"
check_neg EINVAL "snapshot('$fs1@bad@name')"
check_neg EINVAL "snapshot('$fs1@bad/name')"
# snapshots must be in this pool
check_neg EXDEV "snapshot('${TESTPOOL}x/fs@snap')"
check_neg EXDEV "snapshot('ceci_nest_pas_une_pool@snap')"
# rename_snapshot checks the new name, the old name and the filesystem
check_neg EINVAL "rename_snapshot('$fs1', 'snap1', '')"
check_neg EINVAL "rename_snapshot('$fs1', 'snap1', 'bad%name')"
check_neg EINVAL "rename_snapshot('$fs1', 'snap1', 'bad*name')"
check_neg EINVAL "rename_snapshot('$fs1', 'snap1', 'bad@name')"
check_neg ENAMETOOLONG "rename_snapshot('$fs1', 'snap1', string.rep('a', 512))"
check_neg EINVAL "rename_snapshot('$fs1', '', 'snap2')"
check_neg EINVAL "rename_snapshot('$fs1', 'bad%name', 'snap2')"
check_neg EINVAL "rename_snapshot('$fs1@snap1', 'snap1', 'snap2')"
check_neg EXDEV "rename_snapshot('${TESTPOOL}x/fs', 'snap1', 'snap2')"

# Only the snapshots the programs were allowed to create may exist.
snaps=$(zfs list -H -o name -t snapshot -r $fs1 $fs2 | sort | xargs)
[[ "$snaps" == "$fs1@snap1 $fs2@snap1" ]] || \
    log_fail "unexpected snapshots: $snaps"

log_pass "zfs.sync.snapshot returns correct errors on invalid input"

