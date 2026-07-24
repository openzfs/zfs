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
# See the License for the specific language governing permissions and
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
# Copyright (c) 2026 by Heonje LEE. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib
. $STF_SUITE/tests/functional/zvol/zvol_misc/zvol_misc_common.kshlib

#
# DESCRIPTION:
# Verify 'zfs set volmode' on a ZVOL whose block device is in use returns
# EBUSY instead of entering an uninterruptible D-state sleep.
#
# STRATEGY:
# 1. Create a ZVOL, format it, and mount the block device
# 2. Verify changing volmode fails (EBUSY) while the device is mounted
# 3. Unmount and verify volmode can be changed successfully
#

verify_runnable "global"

function cleanup
{
	ismounted "$MNTPFS" $NEWFS_DEFAULT_FS && log_must umount "$MNTPFS"
	[[ -d "$MNTPFS" ]] && log_must rmdir "$MNTPFS"
	datasetexists $ZVOL && destroy_dataset $ZVOL -r
	is_linux && udev_cleanup
}

log_assert "Verify 'zfs set volmode' on in-use ZVOL returns EBUSY"
log_onexit cleanup

ZVOL="$TESTPOOL/vol"
ZDEV="$ZVOL_DEVDIR/$ZVOL"
MNTPFS="$TESTDIR/zvol_inuse_volmode"

# 1. Create a ZVOL, format it, and mount the block device
log_must zfs create -V $VOLSIZE "$ZVOL"
block_device_wait "$ZDEV"
log_must eval "new_fs $ZDEV >/dev/null 2>&1"
log_must mkdir "$MNTPFS"
log_must mount "$ZDEV" "$MNTPFS"

# 2. Verify changing volmode fails while the device is mounted
is_linux && udev_wait
log_mustnot zfs set volmode=none "$ZVOL"
is_linux && udev_wait
log_mustnot zfs set volmode=dev "$ZVOL"

# 3. Unmount and verify volmode can be changed successfully
log_must umount "$MNTPFS"
log_must rmdir "$MNTPFS"
is_linux && udev_wait
log_must zfs set volmode=none "$ZVOL"
blockdev_missing $ZDEV

log_pass "Setting volmode on in-use ZVOL correctly returns EBUSY"
