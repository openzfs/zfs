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
# Copyright (c) 2013 by Jinshan Xiong. No rights reserved.
# Copyright (c) 2017 Datto Inc.
#

. $STF_SUITE/tests/functional/upgrade/upgrade_common.kshlib

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

verify_runnable "global"

log_assert "pool upgrade for userobj accounting should work"
log_onexit cleanup_upgrade

log_must zpool create -d -m $TESTDIR $TESTPOOL $TMPDEV

log_must mkfiles $TESTDIR/tf $((RANDOM % 1000 + 1))
log_must zfs create $TESTPOOL/fs1
log_must mkfiles $TESTDIR/fs1/tf $((RANDOM % 1000 + 1))
log_must zfs create $TESTPOOL/fs2
log_must mkfiles $TESTDIR/fs2/tf $((RANDOM % 1000 + 1))
log_must zfs umount $TESTPOOL/fs2

# Make sure userobj accounting is disabled
zfs userspace -o objused -H $TESTPOOL | head -n 1 | grep -q "-" ||
	log_fail "userobj accounting should be disabled initially"

# Upgrade zpool to support all features
log_must zpool upgrade $TESTPOOL

# Make sure userobj accounting is disabled again
zfs userspace -o objused -H $TESTPOOL | head -n 1 | grep -q "-" ||
	log_fail "userobj accounting should be disabled after pool upgrade"

# Create a file in fs1 should trigger dataset upgrade
log_must mkfile 1m $TESTDIR/fs1/tf
log_must sleep 1 # upgrade done in the background so let's give it a sec

# Make sure userobj accounting is working for fs1
zfs userspace -o objused -H $TESTPOOL/fs1 | head -n 1 | grep -q "-" &&
	log_fail "userobj accounting should be enabled for $TESTPOOL/fs1"

# Mount a dataset should trigger upgrade
log_must zfs mount $TESTPOOL/fs2
log_must sleep 1 # upgrade done in the background so let's give it a sec

# Make sure userobj accounting is working for fs2
zfs userspace -o objused -H $TESTPOOL/fs2 | head -n 1 | grep -q "-" &&
	log_fail "userobj accounting should be enabled for $TESTPOOL/fs2"

# All in all, after having been through this, the dataset for testpool
# still shouldn't be upgraded
zfs userspace -o objused -H $TESTPOOL | head -n 1 | grep -q "-" ||
	log_fail "userobj accounting should be disabled for $TESTPOOL"

# Manual upgrade root dataset
# uses an ioctl which will wait for the upgrade to be done before returning
log_must zfs set version=current $TESTPOOL
zfs userspace -o objused -H $TESTPOOL | head -n 1 | grep -q "-" &&
	log_fail "userobj accounting should be enabled for $TESTPOOL"

log_pass "all tests passed - what a lucky day!"
