#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
