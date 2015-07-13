#!/bin/ksh -p
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy.cfg
. $STF_SUITE/tests/functional/cli_root/zfs_destroy/zfs_destroy_common.kshlib

################################################################################
#
# Deferred destroyed snapshots remain until the last hold is released.
#
# 1. Create test environment without clones.
# 2. 'zfs hold <tag1> <snap>'
# 3. 'zfs destroy -d <snap>'
# 4. Sequence of holds/releases including invalid ones that should fail.
# 4. Verify snapshot still exists.
# 5. Release all holds.
# 6. Verify that the snapshot is destroyed.
#
################################################################################

log_assert "deferred destroyed snapshots remain until last hold is released"
log_onexit cleanup_testenv

setup_testenv snap

for dstype in FS VOL; do
    snap=$(eval echo \$${dstype}SNAP)
    log_must $ZFS hold zfstest1 $snap
    destroy_dataset -d $snap
    log_must datasetexists $snap
    log_must eval "[[ $(get_prop defer_destroy $snap) == 'on' ]]"

    log_must $ZFS hold zfstest2 $snap
    log_mustnot $ZFS hold zfstest1 $snap
    log_mustnot $ZFS hold zfstest2 $snap

    log_must $ZFS release zfstest1 $snap
    log_must datasetexists $snap
    log_mustnot $ZFS release zfstest1 $snap
    log_must datasetexists $snap
    log_mustnot $ZFS release zfstest3 $snap
    log_must datasetexists $snap

    log_must $ZFS release zfstest2 $snap
    log_mustnot datasetexists $snap
done

log_pass "deferred destroyed snapshots remain until last hold is released"
