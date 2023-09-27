#!/bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2019 by Tim Chase. All rights reserved.
# Copyright (c) 2019 Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_trim/zpool_trim.kshlib

#
# DESCRIPTION:
#	Verify 'zpool trim' partial trim.
#
# STRATEGY:
#	1. Create a pool on a single disk and mostly fill it.
#	2. Expand the pool to create new unallocated metaslabs.
#	3. Run 'zpool trim' to only TRIM allocated space maps.
#	4. Verify the disk is least 90% of its original size.
#	5. Run 'zpool trim' to perform a full TRIM.
#	6. Verify the disk is less than 10% of its original size.

function cleanup
{
	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi

	if [[ -d "$TESTDIR" ]]; then
		rm -rf "$TESTDIR"
	fi

	log_must set_tunable64 TRIM_METASLAB_SKIP 0
	log_must set_tunable64 TRIM_EXTENT_BYTES_MIN $trim_extent_bytes_min
	log_must set_tunable64 VDEV_MIN_MS_COUNT $vdev_min_ms_count
}
log_onexit cleanup

LARGESIZE=$((MINVDEVSIZE * 4))
LARGEFILE="$TESTDIR/largefile"

# The minimum number of metaslabs is increased in order to simulate the
# behavior of partial trimming on a more typically sized 1TB disk.
typeset vdev_min_ms_count=$(get_tunable VDEV_MIN_MS_COUNT)
log_must set_tunable64 VDEV_MIN_MS_COUNT 64

# Minimum trim size is decreased to verify all trim sizes.
typeset trim_extent_bytes_min=$(get_tunable TRIM_EXTENT_BYTES_MIN)
log_must set_tunable64 TRIM_EXTENT_BYTES_MIN 512

log_must mkdir "$TESTDIR"
log_must truncate -s $LARGESIZE "$LARGEFILE"
log_must zpool create -O compression=off $TESTPOOL "$LARGEFILE"
log_must mkfile $(( floor(LARGESIZE * 0.80) )) /$TESTPOOL/file
sync_all_pools

new_size=$(du -B1 "$LARGEFILE" | cut -f1)
log_must test $new_size -le $LARGESIZE
log_must test $new_size -gt $(( floor(LARGESIZE * 0.70) ))

# Expand the pool to create new unallocated metaslabs.
log_must zpool export $TESTPOOL
log_must dd if=/dev/urandom of=$LARGEFILE conv=notrunc,nocreat \
    seek=$((LARGESIZE / (1024 * 1024))) bs=$((1024 * 1024)) \
    count=$((3 * LARGESIZE / (1024 * 1024)))
log_must zpool import -d $TESTDIR $TESTPOOL
log_must zpool online -e $TESTPOOL "$LARGEFILE"

new_size=$(du -B1 "$LARGEFILE" | cut -f1)
log_must test $new_size -gt $((4 * floor(LARGESIZE * 0.70) ))

# Perform a partial trim, we expect it to skip most of the new metaslabs
# which have never been used and therefore do not need be trimmed.
log_must set_tunable64 TRIM_METASLAB_SKIP 1
log_must zpool trim $TESTPOOL
log_must set_tunable64 TRIM_METASLAB_SKIP 0

sync_all_pools
while [[ "$(trim_progress $TESTPOOL $LARGEFILE)" -lt "100" ]]; do
	sleep 0.5
done

new_size=$(du -B1 "$LARGEFILE" | cut -f1)
log_must test $new_size -gt $LARGESIZE

# Perform a full trim, all metaslabs will be trimmed the pool vdev
# size will be reduced but not down to its original size due to the
# space usage of the new metaslabs.
log_must zpool trim $TESTPOOL

sync_all_pools
while [[ "$(trim_progress $TESTPOOL $LARGEFILE)" -lt "100" ]]; do
	sleep 0.5
done

new_size=$(du -B1 "$LARGEFILE" | cut -f1)
log_must test $new_size -le $(( 2 * LARGESIZE))
log_must test $new_size -gt $(( floor(LARGESIZE * 0.70) ))

log_pass "Manual 'zpool trim' successfully partially trimmed pool"
