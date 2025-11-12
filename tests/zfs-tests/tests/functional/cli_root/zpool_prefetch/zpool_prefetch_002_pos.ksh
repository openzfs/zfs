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
# Copyright (c) 2025 by iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zpool prefetch -t brt <pool>' can successfully load a pool's BRT on demand.
# 'zpool prefetch <pool>' without -t prefetches both DDT and BRT.
#
# STRATEGY:
# 1. Create a dataset with block cloning enabled.
# 2. Create files and clone them to populate the BRT.
# 3. Export and import the pool to flush caches.
# 4. Use zpool prefetch -t brt to load BRT.
# 5. Test zpool prefetch without -t to prefetch all types.
#

verify_runnable "both"

if ! command -v clonefile > /dev/null ; then
	log_unsupported "clonefile program required to test block cloning"
fi

log_assert "'zpool prefetch' can successfully load BRT and prefetch all types"

DATASET=$TESTPOOL/brt

function cleanup
{
	datasetexists $DATASET && destroy_dataset $DATASET -f
}

log_onexit cleanup
log_must zfs create $DATASET
MNTPOINT=$(get_prop mountpoint $DATASET)

log_note "Generating cloned blocks for BRT ..."

# Create source file
log_must dd if=/dev/urandom of=$MNTPOINT/source bs=1M count=100

# Create clones using clonefile
typeset -i i=0
while (( i < 50 )); do
	log_must clonefile -f $MNTPOINT/source $MNTPOINT/clone.$i
	((i += 1))
done

sync_pool $TESTPOOL

# Verify BRT has entries (non-zero saved space)
brt_saved=$(zpool get -Hp -o value bclone_saved $TESTPOOL)
log_note "BRT saved space: $brt_saved"
log_must test "$brt_saved" -gt "0"

# Export/import to flush caches
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

# Test BRT prefetch - verify command succeeds
# Note: BRT does not expose cache statistics like DDT, so we can only
# verify the prefetch command completes successfully
log_must zpool prefetch -t brt $TESTPOOL

# Test prefetch without -t (should prefetch all types including BRT)
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
log_must zpool prefetch $TESTPOOL

log_pass "'zpool prefetch' successfully loads BRT and all types"
