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
# Copyright (c) 2017 by Fan Yong. All rights reserved.
#

. $STF_SUITE/tests/functional/upgrade/upgrade_common.kshlib

#
# DESCRIPTION:
#
# Check whether zfs upgrade for project quota works or not.
# The project quota is per dataset based feature, this test
# will create multiple datasets and try different upgrade methods.
#
# STRATEGY:
# 1. Create a pool with all features disabled
# 2. Create a few dataset for testing
# 3. Make sure automatic upgrade work
# 4. Make sure manual upgrade work
#

verify_runnable "global"

if ! lsattr -pd > /dev/null 2>&1; then
	log_unsupported "Current lsattr does not support set/show project ID"
fi

log_assert "pool upgrade for projectquota should work"
log_onexit cleanup_upgrade

log_must zpool create -d -m $TESTDIR $TESTPOOL $TMPDEV

log_must mkfiles $TESTDIR/tf $((RANDOM % 100 + 1))
log_must zfs create $TESTPOOL/fs1
log_must mkfiles $TESTDIR/fs1/tf $((RANDOM % 100 + 1))
log_must zfs umount $TESTPOOL/fs1

log_must zfs create $TESTPOOL/fs2
log_must mkdir $TESTDIR/fs2/dir
log_must mkfiles $TESTDIR/fs2/tf $((RANDOM % 100 + 1))

log_must zfs create $TESTPOOL/fs3
log_must mkdir $TESTDIR/fs3/dir
log_must mkfiles $TESTDIR/fs3/tf $((RANDOM % 100 + 1))

# Make sure project quota is disabled
zfs projectspace -o used $TESTPOOL | grep -q "USED" &&
	log_fail "project quota should be disabled initially"

# set projectquota before upgrade will fail
log_mustnot zfs set projectquota@100=100m $TESTDIR/fs3

# set projectobjquota before upgrade will fail
log_mustnot zfs set projectobjquota@100=1000 $TESTDIR/fs3

# 'chattr -p' should fail before upgrade
log_mustnot chattr -p 100 $TESTDIR/fs3/dir

# 'chattr +P' should fail before upgrade
log_mustnot chattr +P $TESTDIR/fs3/dir

# Upgrade zpool to support all features
log_must zpool upgrade $TESTPOOL

# Double check project quota is disabled
zfs projectspace -o used $TESTPOOL | grep -q "USED" &&
	log_fail "project quota should be disabled after pool upgrade"

# Mount dataset should trigger upgrade
log_must zfs mount $TESTPOOL/fs1
log_must sleep 3 # upgrade done in the background so let's wait for a while
zfs projectspace -o used $TESTPOOL/fs1 | grep -q "USED" ||
	log_fail "project quota should be enabled for $TESTPOOL/fs1"

# Create file should trigger dataset upgrade
log_must mkfile 1m $TESTDIR/fs2/dir/tf
log_must sleep 3 # upgrade done in the background so let's wait for a while
zfs projectspace -o used $TESTPOOL/fs2 | grep -q "USED" ||
	log_fail "project quota should be enabled for $TESTPOOL/fs2"

# "lsattr -p" should NOT trigger upgrade
log_must lsattr -p -d $TESTDIR/fs3/dir
zfs projectspace -o used $TESTPOOL/fs3 | grep -q "USED" &&
	log_fail "project quota should not active for $TESTPOOL/fs3"

# 'chattr -p' should trigger dataset upgrade
log_must chattr -p 100 $TESTDIR/fs3/dir
log_must sleep 5 # upgrade done in the background so let's wait for a while
zfs projectspace -o used $TESTPOOL/fs3 | grep -q "USED" ||
	log_fail "project quota should be enabled for $TESTPOOL/fs3"
cnt=$(get_prop projectobjused@100 $TESTPOOL/fs3)
# if 'xattr=on', then 'cnt = 2'
[[ $cnt -ne 1 ]] && [[ $cnt -ne 2 ]] &&
	log_fail "projectquota accounting failed $cnt"

# All in all, after having been through this, the dataset for testpool
# still shouldn't be upgraded
zfs projectspace -o used $TESTPOOL | grep -q "USED" &&
	log_fail "project quota should be disabled for $TESTPOOL"

# Manual upgrade root dataset
# uses an ioctl which will wait for the upgrade to be done before returning
log_must zfs set version=current $TESTPOOL
zfs projectspace -o used $TESTPOOL | grep -q "USED" ||
	log_fail "project quota should be enabled for $TESTPOOL"

log_pass "Project Quota upgrade done"
