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

# Test that 'zfs recv -F' will not destroy/replace a dataset that still has
# mounts.

verify_runnable "both"

if ! is_linux ; then
	log_unsupported "bind mounts only available on Linux"
fi

CLAIM="'zfs recv -F' will not destroy a mounted dataset."

mountpoint=$(mktemp -d)

function cleanup
{
	unmount $mountpoint
	rmdir $mountpoint
	destroy_dataset $TESTPOOL/src@snap
	destroy_dataset $TESTPOOL/src
	destroy_dataset $TESTPOOL/dst
}
log_onexit cleanup

log_assert $CLAIM

log_must create_dataset $TESTPOOL/src
log_must create_dataset $TESTPOOL/dst
log_must create_snapshot $TESTPOOL/src snap

log_must mount --bind $(get_prop mountpoint $TESTPOOL/dst) $mountpoint

log_mustnot eval "zfs send $TESTPOOL/src@snap | zfs recv -F $TESTPOOL/dst"

log_assert $CLAIM
