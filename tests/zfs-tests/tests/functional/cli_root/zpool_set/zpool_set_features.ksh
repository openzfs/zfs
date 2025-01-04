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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2018, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zpool set' should be able to enable features on pools
#
# STRATEGY:
# 1. Create a pool with all features disabled
# 2. Verify 'zpool set' is able to enable a single feature
# 3. Create a pool with all features enabled
# 4. Verify 'zpool set' is *not* able to disable a single feature
# 5. Rinse and repeat for known features
#

verify_runnable "both"

function cleanup
{
	destroy_pool $TESTPOOL1
	rm -f $FILEVDEV
}

log_assert "'zpool set' should be able to enable features on pools"
log_onexit cleanup

typeset -a features=(
    "async_destroy"
    "large_blocks"
    "hole_birth"
    "large_dnode"
    "userobj_accounting"
    "encryption"
)
FILEVDEV="$TEST_BASE_DIR/zpool_set_features.$$.dat"

# Verify 'zpool set' is able to enable a single feature
for feature in "${features[@]}"
do
	propname="feature@$feature"
	truncate -s $SPA_MINDEVSIZE $FILEVDEV
	log_must zpool create -d -f $TESTPOOL1 $FILEVDEV
	log_must zpool set "$propname=enabled" $TESTPOOL1
	propval="$(get_pool_prop $propname $TESTPOOL1)"
	log_must test "$propval" ==  'enabled' -o "$propval" ==  'active'
	cleanup
done
# Verify 'zpool set' is *not* able to disable a single feature
for feature in "${features[@]}"
do
	propname="feature@$feature"
	truncate -s $SPA_MINDEVSIZE $FILEVDEV
	log_must zpool create -f $TESTPOOL1 $FILEVDEV
	log_mustnot zpool set "$propname=disabled" $TESTPOOL1
	propval="$(get_pool_prop $propname $TESTPOOL1)"
	log_must test "$propval" ==  'enabled' -o "$propval" ==  'active'
	cleanup
done

log_pass "'zpool set' can enable features on pools"
