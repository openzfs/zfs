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
#
# zpool upgrade -V shouldn't be able to upgrade a pool to an unknown version
#
# STRATEGY:
# 1. Take an existing pool
# 2. Attempt to upgrade it to an unknown version
# 3. Verify that the upgrade failed, and the pool version was still the original
#

verify_runnable "global"

function cleanup
{
	destroy_upgraded_pool $config
}

log_assert "zpool upgrade -V shouldn't be able to upgrade a pool to" \
    "unknown version"

typeset -i config=2
typeset -n pool_name=ZPOOL_VERSION_${config}_NAME

create_old_pool $config
log_mustnot zpool upgrade -V 999 $pool_name
log_mustnot zpool upgrade -V 999
check_poolversion $pool_name $config
destroy_upgraded_pool $config

log_pass "zpool upgrade -V shouldn't be able to upgrade a pool to" \
    "unknown version"
