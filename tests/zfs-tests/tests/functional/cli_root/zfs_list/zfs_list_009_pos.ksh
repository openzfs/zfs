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
# Copyright (c) 2026 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zfs list -Ho name <path>' follows symlinks when resolving the path to
# a dataset name. A symlink that crosses a mount boundary must resolve to
# the dataset owning the symlink's target, not the dataset containing the
# symlink itself.
#
# STRATEGY:
# 1. Create two child datasets: ds1 and ds2.
# 2. Place a symlink inside ds1 that points into ds2.
# 3. Verify that 'zfs list -Ho name <symlink>' returns ds2.
#

verify_runnable "global"

DS1="$TESTPOOL/$TESTFS/ds1"
DS2="$TESTPOOL/$TESTFS/ds2"
LINK="$TESTDIR/ds1/link_to_ds2"

function cleanup
{
	rm -f "$LINK"
	datasetexists "$DS1" && log_must zfs destroy "$DS1"
	datasetexists "$DS2" && log_must zfs destroy "$DS2"
}

log_onexit cleanup

log_assert "'zfs list -Ho name' follows symlinks when resolving a path."

log_must zfs create "$DS1"
log_must zfs create "$DS2"
log_must ln -s "$TESTDIR/ds2" "$LINK"

result=$(zfs list -Ho name "$LINK")
if [[ "$result" != "$DS2" ]]; then
	log_fail "'zfs list -Ho name $LINK' returned '$result', expected '$DS2'"
fi

log_pass "'zfs list -Ho name' correctly follows a symlink crossing a mount boundary."
