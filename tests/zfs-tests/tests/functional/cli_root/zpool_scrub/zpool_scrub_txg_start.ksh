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

# Copyright (c) 2023, Klara Inc.
#
# This software was developed by
# Mariusz Zaborski <mariusz.zaborski@klarasystems.com>
# under sponsorship from Wasabi Technology, Inc.  and Klara Inc.

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
#	Verify scrub -T <txg>
#
# STRATEGY:
#      1. Create a pool and create two files.
#      2. Corrupt the two files directly on the disks.
#      3. Run a scrub with a txg that is between the birth time of
#         the first file and birth time of the second file.
#      4. Check that only the second file was reported as corrupted.
#      5. Perform a full scrub.
#      6. Check that both files were reported as corrupted.
#

verify_runnable "global"

function cleanup
{
	log_must zinject -c all
	log_must rm -f $mntpnt/f1
	log_must rm -f $mntpnt/f2
}

log_onexit cleanup

log_assert "Verify scrub -T."

# Create two files and save a last txg.
mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)

log_must file_write -b 1048576 -c 10 -o create -d 0 -f $mntpnt/f1
log_must sync_pool $TESTPOOL true
f1txg=$(get_last_txg_synced $TESTPOOL)

log_must file_write -b 1048576 -c 10 -o create -d 0 -f $mntpnt/f2
log_must sync_pool $TESTPOOL true
f2txg=$(get_last_txg_synced $TESTPOOL)

# Make sure that the sync txg are different.
log_must [ $f1txg -ne $f2txg ]

# Insert faults.
log_must zinject -a -t data -e io -T read $mntpnt/f1
log_must zinject -a -t data -e io -T read $mntpnt/f2

# Verify that only second file was detected.
log_must zpool scrub -w -T "${f1txg}" $TESTPOOL
log_must eval "zpool status -v $TESTPOOL | grep '$mntpnt/f2'"
log_mustnot eval "zpool status -v $TESTPOOL | grep '$mntpnt/f1'"

# Verify that both files are corrupted.
log_must zpool scrub -w $TESTPOOL
log_must eval "zpool status -v $TESTPOOL | grep '$mntpnt/f1'"
log_must eval "zpool status -v $TESTPOOL | grep '$mntpnt/f2'"

log_pass "Verified scrub -T show expected status."
