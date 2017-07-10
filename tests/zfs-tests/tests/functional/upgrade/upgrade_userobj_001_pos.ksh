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
# Copyright (c) 2013 by Jinshan Xiong. No rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# Check that zfs upgrade for object count accounting works.
# Since userobjaccounting is a per dataset feature, this test case
# will create multiple dataset and try different upgrade method.
#
# STRATEGY:
# 1. Create a pool with all features disabled
# 2. Create a few dataset for testing
# 3. Make sure automatic upgrade work
# 4. Make sure manual upgrade work
#

function cleanup
{
	datasetexists $TESTPOOL/fs1 && log_must $ZFS destroy $TESTPOOL/fs1
	datasetexists $TESTPOOL/fs2 && log_must $ZFS destroy $TESTPOOL/fs2
}

verify_runnable "global"

log_assert "pool upgrade for userobj accounting should work"
log_onexit cleanup

log_must $MKFILES $TESTDIR/tf $((RANDOM % 1000 + 1))
log_must $ZFS create $TESTPOOL/fs1
log_must $MKFILES $TESTDIR/fs1/tf $((RANDOM % 1000 + 1))
log_must $ZFS create $TESTPOOL/fs2
log_must $MKFILES $TESTDIR/fs2/tf $((RANDOM % 1000 + 1))
log_must $ZFS umount $TESTPOOL/fs2

# Make sure userobj accounting is disabled
$ZFS userspace -o objused -H $TESTPOOL | $HEAD -n 1 | $GREP -q "-" ||
	log_fail "userobj accounting should be disabled initially"

# Upgrade zpool to support all features
log_must $ZPOOL upgrade $TESTPOOL

# Make sure userobj accounting is disabled again
$ZFS userspace -o objused -H $TESTPOOL | $HEAD -n 1 | $GREP -q "-" ||
	log_fail "userobj accounting should be disabled after pool upgrade"

# Create a file in fs1 should trigger dataset upgrade
log_must $MKFILE 1m $TESTDIR/fs1/tf
sync_pool

# Make sure userobj accounting is working for fs1
$ZFS userspace -o objused -H $TESTPOOL/fs1 | $HEAD -n 1 | $GREP -q "-" &&
	log_fail "userobj accounting should be enabled for $TESTPOOL/fs1"

# Mount a dataset should trigger upgrade
log_must $ZFS mount $TESTPOOL/fs2
sync_pool

# Make sure userobj accounting is working for fs2
$ZFS userspace -o objused -H $TESTPOOL/fs2 | $HEAD -n 1 | $GREP -q "-" &&
	log_fail "userobj accounting should be enabled for $TESTPOOL/fs2"

# All in all, after having been through this, the dataset for testpool
# still shouldn't be upgraded
$ZFS userspace -o objused -H $TESTPOOL | $HEAD -n 1 | $GREP -q "-" ||
	log_fail "userobj accounting should be disabled for $TESTPOOL"

# Manual upgrade root dataset
log_must $ZFS set version=current $TESTPOOL
$ZFS userspace -o objused -H $TESTPOOL | $HEAD -n 1 | $GREP -q "-" &&
	log_fail "userobj accounting should be enabled for $TESTPOOL"

log_pass "all tests passed - what a lucky day!"
