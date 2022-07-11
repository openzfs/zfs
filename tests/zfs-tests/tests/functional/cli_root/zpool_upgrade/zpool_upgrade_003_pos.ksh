#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
