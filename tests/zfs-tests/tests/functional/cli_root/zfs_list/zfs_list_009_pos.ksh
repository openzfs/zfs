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
