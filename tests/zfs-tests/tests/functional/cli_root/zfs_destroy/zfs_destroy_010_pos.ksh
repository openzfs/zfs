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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy.cfg
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy_common.kshlib

################################################################################
#
# When using 'zfs destroy -R' on a file system hierarchy that includes a
# snapshot and a clone of that snapshot, and the snapshot has been
# defer-destroyed, make sure that the 'zfs destroy -R' works as expected.
# In particular make sure that libzfs is not confused by the fact that the
# kernel will automatically remove the defer-destroyed snapshot when the
# clone is destroyed.
#
# 1. Create test environment.
# 2. Create a clone of the snapshot.
# 3. 'zfs destroy -d <snap>'
# 4. 'zfs destroy -R'
# 5. Verify that the snapshot and clone are destroyed.
#
################################################################################

function test_clone_run
{
    typeset dstype=$1

    ds=$(eval echo \$${dstype})
    snap=$(eval echo \$${dstype}SNAP)
    clone=$(eval echo \$${dstype}CLONE)
    log_must zfs destroy -d $snap
    log_must datasetexists $snap
    log_must_busy zfs destroy -R $clone
    log_mustnot datasetexists $snap
    log_mustnot datasetexists $clone
}

log_assert "'zfs destroy -R' works on deferred destroyed snapshots"
log_onexit cleanup_testenv

setup_testenv clone

for dstype in FS VOL; do
    if [[ $dstype == VOL ]]; then
		if is_global_zone; then
			test_clone_run $dstype
		fi
	else
		test_clone_run $dstype
	fi
done

log_pass "'zfs destroy -R' works on deferred destroyed snapshots"
