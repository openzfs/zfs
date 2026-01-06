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
# Copyright 2025 SkunkWerks, GmbH
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Test basic use cases of "jail" zfs dataset property.
#
# STRATEGY:
# 1. Create a dataset. Verify.
# 2. Create a jail. Verify.
# 3. Jail the dataset. Verify.
# 4. Mount the dataset by the jail. Verify.
# 5. Unmount the dataset. Verify.
#

verify_runnable "global"

JAIL="testjail"
JAIL_CONF="$STF_SUITE/tests/functional/cli_root/zfs_jail/jail.conf"
DATASET="$TESTPOOL/dataset1"
DATASET_JAILED_MOUNTPOINT="/jailed"

function cleanup
{
	if jls -j $JAIL name >/dev/null 2>&1; then
		jail -r -f $JAIL_CONF $JAIL
	fi
}

log_onexit cleanup

log_assert "Verify basic use cases of jail zfs property."

# Root dataset has default value
log_must test "0" = "$(zfs get -o value -H jail $TESTPOOL)"

# Create the dataset
log_must zfs create -o jailed=on -o mountpoint=$DATASET_JAILED_MOUNTPOINT $DATASET
log_must test "0" = "$(zfs get -o value -H jail $DATASET)"

# Create the jail
log_must jail -c -f $JAIL_CONF $JAIL
log_mustnot jexec $JAIL zfs list $DATASET
log_must test "0" = "$(zfs get -o value -H jail $DATASET)"

# Jail the dataset
log_must zfs jail $JAIL $DATASET
log_must jexec $JAIL zfs list $DATASET
log_must test "0" = "$(zfs get -o value -H jail $DATASET)"
log_must test "0" = "$(jexec $JAIL zfs get -o value -H jail $DATASET)"

# Mount the dataset by the jail
log_must jexec $JAIL zfs mount $DATASET
# Now we see who mounted it
log_must test "$JAIL" = "$(zfs get -o value -H jail $DATASET)"
# But it is hidden from the jail itself
log_must test "0" = "$(jexec $JAIL zfs get -o value -H jail $DATASET)"

# Unmount the dataset by the jail
log_must jexec $JAIL zfs unmount $DATASET
log_must test "0" = "$(zfs get -o value -H jail $DATASET)"
log_must test "0" = "$(jexec $JAIL zfs get -o value -H jail $DATASET)"

log_pass "Verify basic use cases of jail zfs property."
