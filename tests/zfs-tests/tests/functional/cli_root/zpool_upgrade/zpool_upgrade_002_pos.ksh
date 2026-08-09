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
# import pools of all versions - zpool upgrade on each pools works
#
# STRATEGY:
# 1. Execute the command with several invalid options
# 2. Verify a 0 exit status for each
#

verify_runnable "global"

function cleanup
{
	destroy_upgraded_pool $config
}

log_assert "Import pools of all versions - zpool upgrade on each pool works"
log_onexit cleanup

for config in $CONFIGS; do
    create_old_pool $config
    check_upgrade $config
    destroy_upgraded_pool $config
done

log_pass "Import pools of all versions - zpool upgrade on each pool works"
