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
# Copyright (c) 2026, Christos Longros. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify that zfs receive rejects a target dataset that ends in a '.'
# or '..' component.  The received snapshot name is checked as a whole,
# so the component ends at '@' rather than at the end of the name.
#
# Strategy:
# 1. Receive a snapshot into $POOL2/. and $POOL2/.. and verify both fail
# 2. Verify that neither dataset was created
# 3. Verify that $POOL2 can still be listed recursively
# 4. Receive the same snapshot into $POOL2/.dot and verify it succeeds
#

verify_runnable "both"

function cleanup
{
	cleanup_pool $POOL2
}
log_onexit cleanup

log_assert "zfs receive rejects a target ending in a '.' or '..' component"

typeset name
for name in "$POOL2/." "$POOL2/.."; do
	log_mustnot eval "zfs send $POOL/$FS@final | zfs receive $name"
	log_mustnot eval "zfs list -H -o name -d 1 $POOL2 | grep -qxF '$name'"
done
log_must zfs list -r $POOL2

log_must eval "zfs send $POOL/$FS@final | zfs receive $POOL2/.dot"
log_must datasetexists $POOL2/.dot

log_pass "zfs receive rejects a target ending in a '.' or '..' component"
