#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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

function cleanup
{
	log_must zinject -c all
	log_must rm -f $mntpnt/f1
	log_must rm -f $mntpnt/f2
}

log_onexit cleanup

log_assert "Verify scrub -C."

# Create one file.
mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)

log_must file_write -b 1048576 -c 10 -o create -d 0 -f $mntpnt/f1
log_must sync_pool $TESTPOOL true
f1txg=$(get_last_txg_synced $TESTPOOL)

# Verify that last_scrubbed_txg isn't set.
zpoollasttxg=$(zpool get -H -o value last_scrubbed_txg $TESTPOOL)
log_must [ $zpoollasttxg -eq 0 ]

# Run scrub.
log_must zpool scrub -w $TESTPOOL

# Verify that last_scrubbed_txg is set.
zpoollasttxg=$(zpool get -H -o value last_scrubbed_txg $TESTPOOL)
log_must [ $zpoollasttxg -ne 0 ]

# Create second file.
log_must file_write -b 1048576 -c 10 -o create -d 0 -f $mntpnt/f2
log_must sync_pool $TESTPOOL true
f2txg=$(get_last_txg_synced $TESTPOOL)

# Make sure that the sync txg are different.
log_must [ $f1txg -ne $f2txg ]

# Insert faults.
log_must zinject -a -t data -e io -T read $mntpnt/f1
log_must zinject -a -t data -e io -T read $mntpnt/f2

# Run scrub from last saved point.
log_must zpool scrub -w -C $TESTPOOL

# Verify that only newer file was detected.
log_mustnot eval "zpool status -v $TESTPOOL | grep '$mntpnt/f1'"
log_must eval "zpool status -v $TESTPOOL | grep '$mntpnt/f2'"

# Verify that both files are corrupted.
log_must zpool scrub -w $TESTPOOL
log_must eval "zpool status -v $TESTPOOL | grep '$mntpnt/f1'"
log_must eval "zpool status -v $TESTPOOL | grep '$mntpnt/f2'"

log_pass "Verified scrub -C show expected status."
