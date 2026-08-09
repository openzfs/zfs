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
# Copyright (c) 2024 by Nutanix. All rights reserved.
#

. $STF_SUITE/tests/functional/upgrade/upgrade_common.kshlib

#
# DESCRIPTION:
#
# Check DXATTR is intact after sa re-layout by setting projid on old file/dir after upgrade
#
# STRATEGY:
# 1. Create a pool with all features disabled
# 2. Create a dataset for testing
# 3. Set DXATTR on file and directory
# 4. upgrade zpool to support all features
# 5. set project id on file and directory to trigger sa re-layout for projid
# 6. verify DXATTR on file and directory are intact
#

TESTFS=$TESTPOOL/testfs
TESTFSDIR=$TESTDIR/testfs

verify_runnable "global"

log_assert "Check DXATTR is intact after sa re-layout by setting projid on old file/dir after upgrade"
log_onexit cleanup_upgrade

log_must zpool create -d -m $TESTDIR $TESTPOOL $TMPDEV

log_must zfs create -o xattr=sa $TESTFS
log_must mkdir $TESTFSDIR/dir
log_must touch $TESTFSDIR/file
log_must set_xattr test test $TESTFSDIR/dir
log_must set_xattr test test $TESTFSDIR/file

dirino=$(stat -c '%i' $TESTFSDIR/dir)
fileino=$(stat -c '%i' $TESTFSDIR/file)
log_must zpool sync $TESTPOOL
log_must zdb -ddddd $TESTFS $dirino
log_must zdb -ddddd $TESTFS $fileino

log_mustnot chattr -p 100 $TESTFSDIR/dir
log_mustnot chattr -p 100 $TESTFSDIR/file

log_must zpool upgrade $TESTPOOL

log_must chattr -p 100 $TESTFSDIR/dir
log_must chattr -p 100 $TESTFSDIR/file
log_must zpool sync $TESTPOOL
log_must zfs umount $TESTFS
log_must zfs mount $TESTFS
log_must zdb -ddddd $TESTFS $dirino
log_must zdb -ddddd $TESTFS $fileino
log_must get_xattr test $TESTFSDIR/dir
log_must get_xattr test $TESTFSDIR/file

log_pass "Check DXATTR is intact after sa re-layout by setting projid on old file/dir after upgrade"
