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
