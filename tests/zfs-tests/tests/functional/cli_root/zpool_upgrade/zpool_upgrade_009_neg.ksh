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
