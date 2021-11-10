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
. $STF_SUITE/tests/functional/cli_root/zpool_initialize/zpool_initialize.kshlib
. $STF_SUITE/tests/functional/cli_root/zpool_trim/zpool_trim.kshlib

#
# DESCRIPTION:
# After trimming, the disk is actually trimmed.
#
# STRATEGY:
# 1. Create a one-disk pool using a sparse file.
# 2. Initialize the pool and verify the file vdev is no longer sparse.
# 3. Trim the pool and verify the file vdev is again sparse.
#

function cleanup
{
	if poolexists $TESTPOOL; then
		destroy_pool $TESTPOOL
	fi

        if [[ -d "$TESTDIR" ]]; then
                rm -rf "$TESTDIR"
        fi

	log_must set_tunable64 TRIM_EXTENT_BYTES_MIN $trim_extent_bytes_min
}
log_onexit cleanup

LARGESIZE=$((MINVDEVSIZE * 4))
LARGEFILE="$TESTDIR/largefile"

# Reduce trim size to allow for tighter tolerance below when checking.
typeset trim_extent_bytes_min=$(get_tunable TRIM_EXTENT_BYTES_MIN)
log_must set_tunable64 TRIM_EXTENT_BYTES_MIN 4096

log_must mkdir "$TESTDIR"
log_must truncate -s $LARGESIZE "$LARGEFILE"
log_must zpool create $TESTPOOL "$LARGEFILE"

original_size=$(du -B1 "$LARGEFILE" | cut -f1)

log_must zpool initialize $TESTPOOL

while [[ "$(initialize_progress $TESTPOOL $LARGEFILE)" -lt "100" ]]; do
        sleep 0.5
done

new_size=$(du -B1 "$LARGEFILE" | cut -f1)
log_must within_tolerance $new_size $LARGESIZE $((128 * 1024 * 1024))

log_must zpool trim $TESTPOOL

while [[ "$(trim_progress $TESTPOOL $LARGEFILE)" -lt "100" ]]; do
        sleep 0.5
done

new_size=$(du -B1 "$LARGEFILE" | cut -f1)
log_must within_tolerance $new_size $original_size $((128 * 1024 * 1024))

log_pass "Trimmed appropriate amount of disk space"
