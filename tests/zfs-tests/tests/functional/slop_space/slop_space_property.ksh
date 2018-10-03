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
# Pool property 'slopspace' should accept only expected values
#
# STRATEGY:
# 1. Create a pool
# 2. Verify "slopspace" default value (0)
# 3. Verify invalid property values cannot be set
# 4. Verify "slopspace" cannot be set larger than the current free space
#

verify_runnable "both"

function cleanup
{
	destroy_pool $TESTPOOL
	rm -f $SLOPSPACE_FILEDEV
}

log_assert "Pool property 'slopspace' should accept only expected values"
log_onexit cleanup

SLOPSPACE_FILEDEV="$TEST_BASE_DIR/slopspace-filedev"
typeset -a badvalues=("text" "0x" "0xff" "-10" "-")

log_must truncate -s 4g $SLOPSPACE_FILEDEV

# 1. Create a pool
log_must zpool create $TESTPOOL $SLOPSPACE_FILEDEV

# 2. Verify "slopspace" default value (0)
verify_eq "0" "$(get_pool_prop slopspace $TESTPOOL)" "default slopspace"

# 3. Verify invalid property values cannot be set
for value in ${badvalues[@]}; do
	log_mustnot zpool set slopspace=$value $TESTPOOL
done

# 4. Verify "slopspace" cannot be set larger than the current free space
FREESPC=$(get_pool_prop free $TESTPOOL)
# NOTE: use a slightly larger size compared to the estimated "free" value to
# avoid failures due to unaccounted space changes on a live pool
log_mustnot zpool set slopspace=$((FREESPC+1024*1024*2)) $TESTPOOL

log_pass "Pool property 'slopspace' accepts only expected values"
