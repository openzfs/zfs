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
# Copyright (c) 2012 by Delphix. All rights reserved.
# Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zpool_upgrade/zpool_upgrade.kshlib
. $STF_SUITE/tests/functional/cli_root/zfs_upgrade/zfs_upgrade.kshlib

#
# DESCRIPTION:
# import pools of all versions - verify the following operation not break.
#	* zfs create -o version=<vers> <filesystem>
#	* zfs upgrade [-V vers] <filesystem>
#	* zfs set version=<vers> <filesystem>
#
# STRATEGY:
# 1. Import pools of all versions
# 2. Setup a test environment over the old pools.
# 3. Verify the commands related to 'zfs upgrade' succeed as expected.
#

verify_runnable "global"

function cleanup
{
	destroy_upgraded_pool $config
}

POOL_CONFIGS="1raidz 1mirror 2raidz 2mirror 3raidz 3mirror"

log_assert "Import pools of all versions - 'zfs upgrade' on each pool works"
log_onexit cleanup

# $CONFIGS gets set in the .cfg script
for config in $POOL_CONFIGS; do
	typeset -n pool_name=ZPOOL_VERSION_${config}_NAME

	create_old_pool $config
	default_check_zfs_upgrade $pool_name
	destroy_upgraded_pool $config
done

log_pass "Import pools of all versions - 'zfs upgrade' on each pool works"
