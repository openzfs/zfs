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

# Copyright (c) 2023, Klara Inc.
#
# This software was developed by
# Mariusz Zaborski <mariusz.zaborski@klarasystems.com>
# under sponsorship from Wasabi Technology, Inc. and Klara Inc.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
#	Verify scrub -C
#
# STRATEGY:
#      1. Create a pool and create one file.
#      2. Verify that the last_txg_scrub is 0.
#      3. Run scrub.
#      4. Verify that the last_txg_scrub is set.
#      5. Create second file.
#      6. Invalidate both files.
#      7. Run scrub only from last point.
#      8. Verify that only one file, that was created with newer txg,
#         was detected.
#

verify_runnable "global"

VDEV0=$TEST_BASE_DIR/scrub_txg_vdev0
VDEV1=$TEST_BASE_DIR/scrub_txg_vdev1

function cleanup
{
	log_must zinject -c all
	destroy_pool $TESTPOOL2
	log_must rm -f $VDEV0 $VDEV1
}

log_onexit cleanup

log_assert "Verify scrub -C."

# last_scrubbed_txg persists once a scrub completes, so the pool must be
# one no other test has scrubbed.
log_must truncate -s $MINVDEVSIZE $VDEV0 $VDEV1
log_must zpool create -f $TESTPOOL2 mirror $VDEV0 $VDEV1

# Create one file.
mntpnt=$(get_prop mountpoint $TESTPOOL2)

log_must file_write -b 1048576 -c 10 -o create -d 0 -f $mntpnt/f1
log_must sync_pool $TESTPOOL2 true
f1txg=$(get_last_txg_synced $TESTPOOL2)

# Verify that last_scrubbed_txg isn't set.
zpoollasttxg=$(zpool get -H -o value last_scrubbed_txg $TESTPOOL2)
log_must [ $zpoollasttxg -eq 0 ]

# Run scrub.
log_must zpool scrub -w $TESTPOOL2

# Verify that last_scrubbed_txg is set.
zpoollasttxg=$(zpool get -H -o value last_scrubbed_txg $TESTPOOL2)
log_must [ $zpoollasttxg -ne 0 ]

# Create second file.
log_must file_write -b 1048576 -c 10 -o create -d 0 -f $mntpnt/f2
log_must sync_pool $TESTPOOL2 true
f2txg=$(get_last_txg_synced $TESTPOOL2)

# Make sure that the sync txg are different.
log_must [ $f1txg -ne $f2txg ]

# Insert faults.
log_must zinject -a -t data -e io -T read $mntpnt/f1
log_must zinject -a -t data -e io -T read $mntpnt/f2

# Run scrub from last saved point.
log_must zpool scrub -w -C $TESTPOOL2

# Verify that only newer file was detected.
log_mustnot eval "zpool status -v $TESTPOOL2 | grep '$mntpnt/f1'"
log_must eval "zpool status -v $TESTPOOL2 | grep '$mntpnt/f2'"

# Verify that both files are corrupted.
log_must zpool scrub -w $TESTPOOL2
log_must eval "zpool status -v $TESTPOOL2 | grep '$mntpnt/f1'"
log_must eval "zpool status -v $TESTPOOL2 | grep '$mntpnt/f2'"

log_pass "Verified scrub -C show expected status."
