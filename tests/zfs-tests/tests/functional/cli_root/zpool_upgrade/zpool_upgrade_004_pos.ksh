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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
# Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zpool_upgrade/zpool_upgrade.kshlib

#
# DESCRIPTION:
# zpool upgrade -a works
#
# STRATEGY:
# 1. Create all upgradable pools for this system, then upgrade -a
# 2. Verify a 0 exit status
#

verify_runnable "global"

function cleanup
{
	for config in $CONFIGS; do
		destroy_upgraded_pool $config
	done
}

log_assert "zpool upgrade -a works"
log_onexit cleanup

TEST_POOLS=
# Now build all of our pools
for config in $CONFIGS; do
	typeset -n pool_name=ZPOOL_VERSION_${config}_NAME

	TEST_POOLS="$TEST_POOLS $pool_name"
	create_old_pool $config
	check_pool $pool_name pre > /dev/null
done

# upgrade them all at once
export __ZFS_POOL_RESTRICT="$TEST_POOLS"
log_must zpool upgrade -a
unset __ZFS_POOL_RESTRICT

# verify their contents then destroy them
for config in $CONFIGS ; do
	typeset -n pool_name=ZPOOL_VERSION_${config}_NAME

	check_pool $pool_name post > /dev/null
	log_must diff $TEST_BASE_DIR/pool-checksums.$pool_name.pre \
	    $TEST_BASE_DIR/pool-checksums.$pool_name.post
	rm $TEST_BASE_DIR/pool-checksums.$pool_name.pre \
	    $TEST_BASE_DIR/pool-checksums.$pool_name.post
	destroy_upgraded_pool $config
done

log_pass "zpool upgrade -a works"
