#!/bin/ksh -p
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
