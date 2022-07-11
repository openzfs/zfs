#!/bin/ksh -p
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2020 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION: Unmounted canmount=noauto export is removed during zfs share -a
#
# STRATEGY:
# 1. Share a dataset that also has canmount set to noauto
# 2. Capture the zfs exports file when the dataset is mounted + shared
# 3. Simulate a reboot by unmounting the dataset and restoring the exports file
# 4. Verify that 'zfs share -a' removes the export since dataset is not mounted
#

verify_runnable "both"

dataset="$TESTPOOL/$TESTFS"
mountpt=$(get_prop mountpoint $dataset)

function cleanup
{
	zfs set canmount=on $dataset
	zfs set sharenfs=off $dataset
	zfs mount -a

	#
	# unset __ZFS_POOL_EXCLUDE so that we include all file systems when
	# rebuilding the exports file
	#
	unset __ZFS_POOL_EXCLUDE
	rm /etc/exports.d/zfs.exports
	zfs share -a
}

log_assert "Unmounted canmount=noauto export is removed during zfs share -a"
log_onexit cleanup

log_must zfs set canmount=noauto $dataset
zfs mount $dataset > /dev/null 2>&1
log_must mounted $dataset
log_must zfs set sharenfs=on $dataset
log_must is_exported $mountpt

log_must cp /etc/exports.d/zfs.exports /etc/exports.d/zfs.exports.save
log_must zfs umount $dataset
log_must unmounted $dataset
log_mustnot is_exported $mountpt

# simulate a reboot condition
log_must mv /etc/exports.d/zfs.exports.save /etc/exports.d/zfs.exports

log_must is_exported $mountpt
log_must zfs share -a
log_mustnot is_exported $mountpt

log_pass "Unmounted canmount=noauto export is removed during zfs share -a"
