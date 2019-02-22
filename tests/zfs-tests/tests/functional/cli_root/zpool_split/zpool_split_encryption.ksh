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
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_split/zpool_split.cfg

#
# DESCRIPTION:
# 'zpool split' should be able to split encrypted pools
#
# STRATEGY:
# 1. Create an encrypted pool
# 2. Split and re-import the pool, verify altroot is mounted.
#

verify_runnable "both"

function cleanup
{
	destroy_pool $TESTPOOL
	destroy_pool $TESTPOOL2
	rm -f $DEVICE1 $DEVICE2
}

log_assert "'zpool split' should be able to split encrypted pools"
log_onexit cleanup

DEVICE1="$TEST_BASE_DIR/device-1"
DEVICE2="$TEST_BASE_DIR/device-2"
passphrase="password"
altroot="$TESTDIR/zpool-split-$RANDOM"

# 1. Create an encrypted pool
truncate -s $SPA_MINDEVSIZE $DEVICE1
truncate -s $SPA_MINDEVSIZE $DEVICE2
log_must eval "echo "$passphrase" | zpool create -O encryption=aes-256-ccm " \
	"-O keyformat=passphrase $TESTPOOL mirror $DEVICE1 $DEVICE2"

# 2. Split and re-import the pool, verify altroot is mounted.
log_must eval "echo "$passphrase" | zpool split -l -R $altroot " \
	"$TESTPOOL $TESTPOOL2"
log_must test "$(get_prop 'encryption' $TESTPOOL2)" == "aes-256-ccm"
log_must test "$(get_pool_prop 'altroot' $TESTPOOL2)" == "$altroot"
log_must mounted $altroot/$TESTPOOL2

log_pass "'zpool split' can split encrypted pools"
