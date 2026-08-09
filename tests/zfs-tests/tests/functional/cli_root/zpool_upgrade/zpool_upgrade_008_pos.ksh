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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_upgrade/zpool_upgrade.kshlib

#
# DESCRIPTION:
#
# zpool upgrade should be able to upgrade pools to a given version using -V
#
# STRATEGY:
# 1. For all versions pools that can be upgraded on a given OS version
#    (latest pool version - 1)
# 2. Pick a version that's a random number, greater than the version
#    we're running.
# 3. Attempt to upgrade that pool to the given version
# 4. Check the pool was upgraded correctly.
#

verify_runnable "global"

function cleanup
{
	destroy_upgraded_pool $ver_old
}

log_assert "zpool upgrade should be able to upgrade pools to a given version" \
    "using -V"

log_onexit cleanup

# We're just using the single disk version of the pool, which should be
# enough to determine if upgrade works correctly. Also set a MAX_VER
# variable, which specifies the highest version that we should expect
# a zpool upgrade operation to succeed from.
VERSIONS="1 2 3 4 5 6 7 8 9 10 11 12 13 14 15"
MAX_VER=15

for ver_old in $VERSIONS; do
	typeset -n pool_name=ZPOOL_VERSION_${ver_old}_NAME
	typeset -i ver_new=$(random_int_between $ver_old $MAX_VER)

	create_old_pool $ver_old
	log_must eval 'zpool upgrade -V $ver_new $pool_name > /dev/null'
	check_poolversion $pool_name $ver_new
	destroy_upgraded_pool $ver_old
done

log_pass "zpool upgrade should be able to upgrade pools to a given version" \
    "using -V"
