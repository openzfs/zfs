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
. $STF_SUITE/tests/functional/cli_root/zpool_split/zpool_split.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

#
# DESCRIPTION:
# 'zpool split' can set new property values on the new pool
#
# STRATEGY:
# 1. Create a mirror pool
# 2. Verify 'zpool split' can set property values on the new pool, but only if
#    they are valid.
#

verify_runnable "both"

function cleanup
{
	destroy_pool $TESTPOOL
	destroy_pool $TESTPOOL2
	rm -f $DEVICE1 $DEVICE2
	! is_freebsd && log_must mmp_clear_hostid
}

function setup_mirror
{
	truncate -s $SPA_MINDEVSIZE $DEVICE1
	truncate -s $SPA_MINDEVSIZE $DEVICE2
	log_must zpool create -f $TESTPOOL mirror $DEVICE1 $DEVICE2
}

log_assert "'zpool split' can set new property values on the new pool"
log_onexit cleanup

DEVICE1="$TEST_BASE_DIR/device-1"
DEVICE2="$TEST_BASE_DIR/device-2"

typeset good_props=('comment=text' 'ashift=12' 'multihost=on'
    'listsnapshots=on' 'autoexpand=on' 'autoreplace=on'
    'delegation=off' 'failmode=continue')
typeset bad_props=("bootfs=$TESTPOOL2/bootfs" 'version=28' 'ashift=4'
    'allocated=1234' 'capacity=5678' 'multihost=none'
    'feature@async_destroy=disabled' 'feature@xxx_fake_xxx=enabled'
    'propname=propval' 'readonly=on')
if ! is_freebsd; then
	good_props+=('multihost=on')
	bad_props+=('multihost=none')
	if [ -e $HOSTID_FILE ]; then
		log_unsupported "System has existing $HOSTID_FILE file"
	fi
	# Needed to set multihost=on
	log_must mmp_set_hostid $HOSTID1
fi

# Verify we can set a combination of valid property values on the new pool
for prop in "${good_props[@]}"
do
	IFS='=' read -r propname propval <<<"$prop"
	setup_mirror
	log_must zpool split -o $prop $TESTPOOL $TESTPOOL2
	log_must zpool import -N -d $TEST_BASE_DIR $TESTPOOL2
	log_must test "$(get_pool_prop $propname $TESTPOOL2)" = "$propval"

	destroy_pool $TESTPOOL
	destroy_pool $TESTPOOL2
	rm -f $DEVICE1 $DEVICE2
done

# Verify we cannot set invalid property values
setup_mirror
zfs create $TESTPOOL/bootfs
for prop in "${bad_props[@]}"
do
	log_mustnot zpool split -o $prop $TESTPOOL $TESTPOOL2
	log_mustnot zpool import -N -d $TEST_BASE_DIR $TESTPOOL2
done

log_pass "'zpool split' can set new property values on the new pool"
