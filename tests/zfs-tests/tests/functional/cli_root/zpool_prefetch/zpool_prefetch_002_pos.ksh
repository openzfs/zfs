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
