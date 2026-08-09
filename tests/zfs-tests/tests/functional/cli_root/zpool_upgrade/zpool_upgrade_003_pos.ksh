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
# Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zpool_upgrade/zpool_upgrade.kshlib

#
# DESCRIPTION:
# Upgrading a pool that has already been upgraded succeeds.
#
# STRATEGY:
# 1. Upgrade a pool, then try to upgrade it again
# 2. Verify a 0 exit status
#

verify_runnable "global"

function cleanup
{
	destroy_upgraded_pool 1
}

log_assert "Upgrading a pool that has already been upgraded succeeds"
log_onexit cleanup

# Create a version 1 pool
create_old_pool 1
check_upgrade 1
check_upgrade 1
destroy_upgraded_pool 1

log_pass "Upgrading a pool that has already been upgraded succeeds"
