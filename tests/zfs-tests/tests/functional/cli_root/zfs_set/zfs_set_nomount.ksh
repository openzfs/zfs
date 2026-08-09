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
# Copyright (c) 2023 by iXsystems, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_set/zfs_set_common.kshlib

#
# DESCRIPTION:
# 'zfs set -u' should update the mountpoint, sharenfs and sharesmb
# properties without mounting and sharing the dataset. Validate the
# bevaior while dataset is mounted and unmounted.
#
# STRATEGY:
# 1. Confirm dataset is currently mounted
# 2. Update the mountpoint with -u flag
# 3. Confirm mountpoint property is updated with new value
# 4. Confirm dataset is still mounted at previous mountpoint
# 5. Unmount the dataset
# 6. Confirm dataset is unmounted
# 7. Mount the dataset
# 8. Confirm dataset is mounted at new mountpoint, that was set with -u flag.
# 9. Update and mount the dataset at previous mountpoint.
# 10. Unmount the dataset
# 11. Update mountpoint property with zfs set -u
# 12. Confirm dataset is not mounted
# 13. Update sharenfs property with zfs set -u
# 14. Confirm dataset is not mounted
# 15. Update sharesmb property with zfs set -u
# 16. Confirm dataset is not mounted
# 17. Mount the dataset and confirm dataset is mounted at new mountpoint
#

verify_runnable "both"

function cleanup
{
	log_must zfs set sharenfs=off $TESTPOOL/$TESTFS
	if is_linux; then
		log_must zfs set sharesmb=off $TESTPOOL/$TESTFS
	fi
	rm -r $newmpt
}

log_assert "'zfs set -u' sets the mountpoint and share properties without " \
	"mounting the dataset"
log_onexit cleanup

oldmpt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
newmpt=$TEST_BASE_DIR/abc

# Test while dataset is mounted
log_must ismounted $TESTPOOL/$TESTFS
log_must zfs set -u mountpoint=$newmpt $TESTPOOL/$TESTFS
log_must check_user_prop $TESTPOOL/$TESTFS mountpoint $newmpt
log_must eval "[[ "$(mount | grep $TESTPOOL/$TESTFS | awk '{print $3}')" == $oldmpt ]]"
log_must zfs unmount $TESTPOOL/$TESTFS
log_mustnot ismounted $TESTPOOL/$TESTFS
log_must zfs mount $TESTPOOL/$TESTFS
log_must eval "[[ "$(mount | grep $TESTPOOL/$TESTFS | awk '{print $3}')" == $newmpt ]]"

# Test while dataset is unmounted
log_must zfs set mountpoint=$oldmpt $TESTPOOL/$TESTFS
log_must ismounted $TESTPOOL/$TESTFS
log_must zfs unmount $TESTPOOL/$TESTFS
log_must zfs set -u mountpoint=$newmpt $TESTPOOL/$TESTFS
log_mustnot ismounted $TESTPOOL/$TESTFS
log_must zfs set -u sharenfs=on $TESTPOOL/$TESTFS
log_mustnot ismounted $TESTPOOL/$TESTFS
if is_linux; then
	log_must zfs set -u sharesmb=on $TESTPOOL/$TESTFS
	log_mustnot ismounted $TESTPOOL/$TESTFS
fi
log_must zfs mount $TESTPOOL/$TESTFS
log_must check_user_prop $TESTPOOL/$TESTFS mountpoint $newmpt
log_must eval "[[ "$(mount | grep $TESTPOOL/$TESTFS | awk '{print $3}')" == $newmpt ]]"

log_must zfs set mountpoint=$oldmpt $TESTPOOL/$TESTFS
log_must ismounted $TESTPOOL/$TESTFS

log_pass "'zfs set -u' functions correctly"
