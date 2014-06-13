#!/usr/bin/ksh -p
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
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_upgrade/zpool_upgrade.kshlib

#
# DESCRIPTION:
#
# Zpool upgrade should be able to upgrade pools to a given version using -V
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
	destroy_upgraded_pool $config
}

log_assert \
 "Zpool upgrade should be able to upgrade pools to a given version using -V"

log_onexit cleanup

# We're just using the single disk version of the pool, which should be
# enough to determine if upgrade works correctly. Also set a MAX_VER
# variable, which specifies the highest version that we should expect
# a zpool upgrade operation to succeed from. (latest version - 1)
CONFIGS="1 2 3 4 5 6 7 8 9 10 11 12 13 14 15"
MAX_VER=15

for config in $CONFIGS
do
        create_old_pool $config
	pool=$(eval $ECHO \$ZPOOL_VERSION_${config}_NAME)
	NEXT=$(random $config $MAX_VER)
	log_must $ZPOOL upgrade -V $NEXT $pool
        check_poolversion $pool $NEXT
        destroy_upgraded_pool $config
done

log_pass "zpool upgrade should be able to upgrade pools to a given version " \
    "using -V"
