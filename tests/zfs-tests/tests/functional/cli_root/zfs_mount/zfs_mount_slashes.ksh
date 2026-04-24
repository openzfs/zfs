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
	rm -rf /mnt/slashes
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
