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
# Verify 'zfs set snapdev=hidden' on a ZVOL whose snapshot device is in use
# returns EBUSY instead of entering an uninterruptible D-state sleep.
#
# STRATEGY:
# 1. Create a ZVOL with snapdev=visible and take a snapshot
# 2. Open the snapshot block device to simulate in-use state
#    (snapshot devices are read-only, so new_fs cannot be used)
# 3. Verify setting snapdev=hidden fails (EBUSY) while device is open
# 4. Release the device and verify snapdev=hidden succeeds
#

verify_runnable "global"

HOLD_PID=""

function cleanup
{
	[[ -n "$HOLD_PID" ]] && kill "$HOLD_PID" 2>/dev/null
	[[ -n "$HOLD_PID" ]] && wait "$HOLD_PID" 2>/dev/null
	datasetexists $ZVOL && destroy_dataset $ZVOL -r
	is_linux && udev_cleanup
}

log_assert "Verify 'zfs set snapdev=hidden' on in-use ZVOL returns EBUSY"
log_onexit cleanup

ZVOL="$TESTPOOL/vol"
SNAP="$ZVOL@snap"
SNAPDEV="$ZVOL_DEVDIR/$SNAP"

# 1. Create a ZVOL with snapdev=visible and take a snapshot
log_must zfs create -V $VOLSIZE "$ZVOL"
log_must zfs set snapdev=visible "$ZVOL"
log_must zfs snapshot "$SNAP"
blockdev_exists "$SNAPDEV"

# 2. Open the snapshot block device to simulate in-use state.
#    Snapshot devices are read-only, so new_fs (mkfs.ext4) cannot be used;
#    instead, hold the device open with a background process.
sleep 600 < "$SNAPDEV" &
HOLD_PID=$!

# 3. Verify setting snapdev=hidden fails while device is open
is_linux && udev_wait
log_mustnot zfs set snapdev=hidden "$ZVOL"

# 4. Release the device and verify snapdev=hidden succeeds
kill "$HOLD_PID" 2>/dev/null
wait "$HOLD_PID" 2>/dev/null
HOLD_PID=""
is_linux && udev_wait
log_must zfs set snapdev=hidden "$ZVOL"
blockdev_missing "$SNAPDEV"

log_pass "Setting snapdev=hidden on in-use snapshot ZVOL returns EBUSY"
