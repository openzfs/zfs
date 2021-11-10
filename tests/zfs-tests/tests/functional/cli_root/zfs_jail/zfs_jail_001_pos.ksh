#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright 2020 iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Test basic functionality of `zfs jail` and `zfs unjail`.
#
# STRATEGY:
# 1. Create a jail.
# 2. Perform some basic ZFS operations on a dataset both in the host and
#    in the jail to confirm the dataset is functional in the host
#    and hidden in in the jail.
# 3. Run `zfs jail` to expose the dataset in the jail.
# 4. Perform some basic ZFS operations on the dataset both in the host and
#    in the jail to confirm the dataset is functional in the jail and host.
# 5. Run `zfs unjail` to return the dataset to the host.
# 6. Perform some basic ZFS operations on the dataset both in the host and
#    in the jail to confirm the dataset is functional in the host
#    and hidden in in the jail.
#

verify_runnable "global"

JAIL="testjail"
JAIL_CONF="$STF_SUITE/tests/functional/cli_root/zfs_jail/jail.conf"

function cleanup
{
	if jls -j $JAIL name >/dev/null 2>&1; then
		jail -r -f $JAIL_CONF $JAIL
	fi
}

log_onexit cleanup

log_assert "Verify that a dataset can be jailed and unjailed."

# 1. Create a jail.
log_must jail -c -f $JAIL_CONF $JAIL

# 2. Try some basic ZFS operations.
log_must zfs list $TESTPOOL
log_mustnot jexec $JAIL zfs list $TESTPOOL

# 3. Jail the dataset.
log_must zfs jail $JAIL $TESTPOOL

# 4. Try some basic ZFS operations.
log_must zfs list $TESTPOOL
log_must jexec $JAIL zfs list $TESTPOOL

# 5. Unjail the dataset.
log_must zfs unjail $JAIL $TESTPOOL

# 6. Try some basic ZFS operations.
log_must zfs list $TESTPOOL
log_mustnot jexec $JAIL zfs list $TESTPOOL

log_pass "Datasets can be jailed and unjailed."
