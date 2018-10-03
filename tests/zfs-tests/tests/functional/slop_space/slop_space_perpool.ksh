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
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/math.shlib
. $STF_SUITE/tests/functional/slop_space/slop_space.cfg

#
# DESCRIPTION:
# The "slopspace" pool property can be used to reserve space on a per-pool basis
#
# STRATEGY:
# 1. Create a small pool and a big(er) pool
# 2. Record current free space on both pools
# 3. Verify setting "slopspace" on one pool does not affect free space on the
#    other pool
# 4. Verify setting "slopspace" back to the default value restore the recorded
#    space
#

verify_runnable "both"

function cleanup
{
	destroy_pool $SMALL_POOL
	destroy_pool $BIG_POOL
	rm -f $SMALL_FILEDEV $BIG_FILEDEV
}

log_assert "slopspace property can be used to reserve space on a per-pool basis"
log_onexit cleanup

SMALL_FILEDEV="$TEST_BASE_DIR/small-filedev"
BIG_FILEDEV="$TEST_BASE_DIR/big-filedev"
SMALL_POOL="$TESTPOOL-small"
BIG_POOL="$TESTPOOL-big"

log_must truncate -s 128m $SMALL_FILEDEV
log_must truncate -s 4g $BIG_FILEDEV

# 1. Create a small pool and a big(er) pool
log_must zpool create -O mountpoint=none $SMALL_POOL $SMALL_FILEDEV
log_must zpool create -O mountpoint=none $BIG_POOL $BIG_FILEDEV

# 2. Record current free space on both pools
SMALL_FREESPC="$(get_prop available $SMALL_POOL)"
BIG_FREESPC="$(get_prop available $BIG_POOL)"

# 3. Verify setting "slopspace" on one pool does not affect free space on the
#    other pool
log_must zpool set slopspace=512M $BIG_POOL
NEW_FREESPC="$(get_prop available $SMALL_POOL)"
# NOTE: due to pool activity space may not be *exactly* equal
log_must within_percent "$SMALL_FREESPC" "$NEW_FREESPC" 99.9

# 4. Verify setting "slopspace" back to the default value restore the recorded
#    space
log_must zpool set slopspace=0 $BIG_POOL
NEW_FREESPC="$(get_prop available $BIG_POOL)"
# NOTE: due to pool activity space may not be *exactly* equal
log_must within_percent "$BIG_FREESPC" "$NEW_FREESPC" 99.9

log_pass "slopspace property can be used to reserve space on a per-pool basis"
