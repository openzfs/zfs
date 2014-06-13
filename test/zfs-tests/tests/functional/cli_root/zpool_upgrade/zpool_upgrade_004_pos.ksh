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
# zpool upgrade -a works
#
# STRATEGY:
# 1. Create all upgradable pools for this system, then upgrade -a
# 2. Verify a 0 exit status
#

verify_runnable "global"

function cleanup
{
	for config in $CONFIGS ; do
		destroy_upgraded_pool $config
	done
}

log_assert "zpool upgrade -a works"
log_onexit cleanup

TEST_POOLS=
# Now build all of our pools
for config in $CONFIGS
do
	POOL_NAME=$(eval $ECHO \$ZPOOL_VERSION_${config}_NAME)

	TEST_POOLS="$TEST_POOLS $POOL_NAME"
	create_old_pool $config
	# a side effect of the check_pool here, is that we get a checksum written
	# called /$TESTPOOL/pool-checksums.$POOL.pre
	check_pool $POOL_NAME pre > /dev/null
done

# upgrade them all at once
export __ZFS_POOL_RESTRICT="$TEST_POOLS"
log_must $ZPOOL upgrade -a
unset __ZFS_POOL_RESTRICT

# verify their contents then destroy them
for config in $CONFIGS
do
	POOL_NAME=$(eval $ECHO \$ZPOOL_VERSION_${config}_NAME)

	check_pool $POOL_NAME post > /dev/null

	# a side effect of the check_pool here, is that we get a checksum written
	# called /$TESTPOOL/pool-checksums.$POOL_NAME.post
	log_must $DIFF /$TESTPOOL/pool-checksums.$POOL_NAME.pre \
		/$TESTPOOL/pool-checksums.$POOL_NAME.post

	$RM /$TESTPOOL/pool-checksums.$POOL_NAME.pre /$TESTPOOL/pool-checksums.$POOL_NAME.post
	destroy_upgraded_pool $config
done

log_pass "zpool upgrade -a works"
