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
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy.cfg
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy_common.kshlib

################################################################################
#
# When using 'zfs destroy -R' on a file system heirarchy that inclues a
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

log_assert "'zfs destroy -R' works on deferred destroyed snapshots"
log_onexit cleanup_testenv

setup_testenv clone
log_must $UMOUNT -f $TESTDIR1

for dstype in FS VOL; do
    ds=$(eval echo \$${dstype})
    snap=$(eval echo \$${dstype}SNAP)
    clone=$(eval echo \$${dstype}CLONE)
    destroy_dataset -d $snap
    log_must datasetexists $snap
    destroy_dataset -R $ds
    log_mustnot datasetexists $snap
    log_mustnot datasetexists $clone
done

log_pass "'zfs destroy -R' works on deferred destroyed snapshots"
