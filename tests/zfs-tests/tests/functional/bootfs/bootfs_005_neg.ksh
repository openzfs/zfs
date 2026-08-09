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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2017 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_upgrade/zpool_upgrade.kshlib

#
# DESCRIPTION:
#
# Boot properties cannot be set on pools with older versions
#
# STRATEGY:
# 1. Copy and import some pools of older versions
# 2. Create a filesystem on each
# 3. Verify that zpool set bootfs fails on each
#

verify_runnable "global"

function cleanup {

	#
	# we need destroy pools that created on top of $TESTPOOL first
	#
	typeset pool_name
	for config in $CONFIGS; do
		pool_name=$(eval echo \$ZPOOL_VERSION_${config}_NAME)
		destroy_pool $pool_name
	done

	destroy_pool $TESTPOOL
}

log_assert "Boot properties cannot be set on pools with older versions"

# These are configs from zpool_upgrade.cfg - see that file for more info.
CONFIGS="1 2 3"

log_onexit cleanup
log_must zpool create -f $TESTPOOL $DISKS

for config in $CONFIGS
do
	create_old_pool $config
	POOL_NAME=$(eval echo \$ZPOOL_VERSION_${config}_NAME)
	log_must zfs create $POOL_NAME/$TESTFS
	log_mustnot zpool set bootfs=$POOL_NAME/$TESTFS $POOL_NAME
	log_must destroy_upgraded_pool $config
done

log_pass "Boot properties cannot be set on pools with older versions"
