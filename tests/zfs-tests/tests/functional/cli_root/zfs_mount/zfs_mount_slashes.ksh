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
# Copyright (c) 2026, TrueNAS.
#

. $STF_SUITE/include/libtest.shlib

#
# Ensure that datasets are mounted in the correct order even when their
# mountpoint= parameters have extra/redundant slashes.
#

verify_runnable "both"

function cleanup
{
	zfs unmount -a
	zfs destroy -R $TESTPOOL/aaa
	zfs destroy -R $TESTPOOL/zzz
	rm -rf /mnt/tree
	zfs mount -a
}
log_onexit cleanup

log_assert "Mountpoints with redundant slashes still mount in the correct order."

# We create the following set of filesystems:
#
# dataset		mountpoint
# testpool/zzz		/mnt/tree/
# testpool/zzz/a	/mnt/tree//a		(inherited)
# testpool/zzz/a/b	/mnt/tree//a/b		(inherited)
# testpool/aaa		/mnt/tree/a/b//c
# testpool/aaa/d	/mnt/tree/a/b//c/d	(inherited)
#
# The dataset names and the creation order are deliberately set such that if
# for whatever reason mountpoint_cmp() falls back to comparing dataset names
# or relying on creation order, the mountpoint order will be wrong. The only
# way that the mountpoints can be ordered correctly is if mountpoint_cmp()
# understands what to do with extra interior and trailing slashes.
#
# If the mounts are performed in the wrong order, they will often still
# succeed, just not be visible in the filesystem. To handle this, we also add a
# file to the root of each filesystem with its expected position. If the mounts
# are out-of-order, these will be hidden "under" the top filesystem, and we
# will know that the order was wrong.

log_must zfs unmount -a

log_must zfs create -o mountpoint=/mnt/tree/a/b//c $TESTPOOL/aaa
log_must touch /mnt/tree/a/b/c/ident-c
log_must zfs create $TESTPOOL/aaa/d
log_must touch /mnt/tree/a/b/c/d/ident-d
log_must zfs unmount -a

log_must zfs create -o mountpoint=/mnt/tree/ $TESTPOOL/zzz
log_must touch /mnt/tree/ident-tree
log_must zfs create $TESTPOOL/zzz/a
log_must touch /mnt/tree/a/ident-a
log_must zfs create $TESTPOOL/zzz/a/b
log_must touch /mnt/tree/a/b/ident-b
log_must zfs unmount -a

# Mount everything, forcing mountpoint order to be resolved
log_must zfs mount -a

# Ensure all mounted, and that the expected filesystem appears at each point.
# `mount` and `df` can produce different results for reasons, check them both.
log_must test $(df | grep /mnt/tree | wc -l) == 5
log_must test $(mount | grep /mnt/tree | wc -l) == 5

# Finally, test that all the files are visible.
log_must test -f /mnt/tree/ident-tree
log_must test -f /mnt/tree/a/ident-a
log_must test -f /mnt/tree/a/b/ident-b
log_must test -f /mnt/tree/a/b/c/ident-c
log_must test -f /mnt/tree/a/b/c/d/ident-d

log_pass "Mountpoints with redundant slashes still mount in the correct order."
