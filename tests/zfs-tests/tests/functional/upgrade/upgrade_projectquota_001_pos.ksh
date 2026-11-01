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
# Copyright (c) 2017 by Fan Yong. All rights reserved.
#

. $STF_SUITE/tests/functional/upgrade/upgrade_common.kshlib

#
# DESCRIPTION:
#
# Check whether zpool upgrade for project quota works or not.
#
# STRATEGY:
# 1. Create a pool with projectquota features disabled
# 2. Create a dataset for testing
# 3. Validate projectquota upgrade works
#

verify_runnable "global"

if ! lsattr -pd > /dev/null 2>&1; then
	log_unsupported "Current lsattr does not support set/show project ID"
fi

log_assert "pool upgrade for projectquota should work"
log_onexit cleanup_upgrade

log_must zpool create -o feature@project_quota=disabled -m $TESTDIR $TESTPOOL $TMPDEV

log_must zfs create $TESTPOOL/fs3
log_must mkdir $TESTDIR/fs3/dir
log_must mkfiles $TESTDIR/fs3/tf $((RANDOM % 100 + 1))
log_must set_xattr_stdin passwd $TESTDIR/fs3/dir < /etc/passwd

# Make sure project quota is disabled
zpool get feature@project_quota -ovalue -H $TESTPOOL | grep -q "disabled" ||
	log_fail "project quota should be disabled initially"

# set projectquota before upgrade will fail
log_mustnot zfs set projectquota@100=100m $TESTPOOL/fs3

# set projectobjquota before upgrade will fail
log_mustnot zfs set projectobjquota@100=1000 $TESTPOOL/fs3

# 'chattr -p' should fail before upgrade
log_mustnot chattr -p 100 $TESTDIR/fs3/dir

# 'chattr +P' should fail before upgrade
log_mustnot chattr +P $TESTDIR/fs3/dir

# Upgrade zpool to support all features
log_must zpool upgrade $TESTPOOL
zpool get feature@project_quota -ovalue -H $TESTPOOL

# pool upgrade should enable project quota
zpool get feature@project_quota -ovalue -H $TESTPOOL | grep -q "enabled" ||
	log_fail "project quota should be enabled after pool upgrade"

# set projectquota should succeed after upgrade
log_must zfs set projectquota@100=100m $TESTPOOL/fs3

# set projectobjquota should succeed after upgrade
log_must zfs set projectobjquota@100=1000 $TESTPOOL/fs3

# 'chattr -p' should succeed after upgrade
log_must chattr -p 100 $TESTDIR/fs3/dir

# 'chattr +P' should succeed after upgrade
log_must chattr +P $TESTDIR/fs3/dir

# project quota should be active
zpool get feature@project_quota -ovalue -H $TESTPOOL | grep -q "active" ||
	log_fail "project quota should be active after chattr"
# project id 100 usage should be accounted
zfs projectspace -o name -H $TESTPOOL/fs3 | grep -q "100" ||
	log_fail "project id 100 usage should be accounted for $TESTPOOL/fs3"

# xattr inodes should be accounted in project quota
dirino=$(stat -c '%i' $TESTDIR/fs3/dir)
log_must zdb -ddddd $TESTPOOL/fs3 $dirino
xattrdirino=$(zdb -ddddd $TESTPOOL/fs3 $dirino |grep -w "xattr" |awk '{print $2}')
echo "xattrdirino: $xattrdirino"
expectedcnt=1
echo "expectedcnt: $expectedcnt"
if [ "$xattrdirino" != "" ]; then
	expectedcnt=$(($expectedcnt + 1))
	echo "expectedcnt: $expectedcnt"
	log_must zdb -ddddd $TESTPOOL/fs3 $xattrdirino
	xattrinocnt=$(zdb -ddddd $TESTPOOL/fs3 $xattrdirino |grep -w "(type:" |wc -l)
	echo "xattrinocnt: $xattrinocnt"
	expectedcnt=$(($expectedcnt + $xattrinocnt))
	echo "expectedcnt: $expectedcnt"
fi
cnt=$(get_prop projectobjused@100 $TESTPOOL/fs3)
[[ $cnt -ne $expectedcnt ]] &&
	log_fail "projectquota accounting failed $cnt"

log_pass "Project Quota upgrade done"
