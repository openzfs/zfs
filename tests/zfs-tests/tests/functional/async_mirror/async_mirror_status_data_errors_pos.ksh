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
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	A pool with permanent data errors shows the data corruption status
#	when the async mirror froze for another reason than a read error of
#	the copy.  Only a freeze after a read error, which logs that read
#	error itself, shows the frozen mirror in place of the data errors.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	   Write a file and wait until the lagging child has caught up.
#	2. Export and import the pool, inject checksum errors on the file,
#	   read it, and clear the injection.  Verify that the pool has data
#	   errors.
#	3. Stall the lagging child until the async mirror gives up on it and
#	   freezes it.
#	4. Verify that 'zpool status' and 'zpool status -x' show the data
#	   corruption status and not the frozen mirror status, and that the
#	   async mirror line still shows the give-up.
#

verify_runnable "global"

function cleanup
{
	zinject -c all > /dev/null 2>&1
	cleanup_async_mirror
}

log_onexit cleanup
log_assert "Data errors are not hidden by a freeze that is no read error."

typeset status

async_mirror_create_pool
log_must zfs set compression=off $TESTPOOL
async_mirror_enable

async_mirror_write /$TESTPOOL/bad 4
sync_pool $TESTPOOL
log_must wait_async_mirror_idle

log_must zpool export $TESTPOOL
async_mirror_import
log_must zinject -t data -e checksum -f 100 -am /$TESTPOOL/bad
dd if=/$TESTPOOL/bad of=/dev/null bs=128k || true
log_must zinject -c all
log_mustnot check_pool_status $TESTPOOL errors "No known data errors"

async_mirror_freeze_by_stall /$TESTPOOL/w
log_must test "$(get_async_mirror_stat held_limit)" -eq 1

status=$(zpool status $TESTPOOL)
log_note "$status"
echo "$status" | grep -q 'resulting in data' ||
	log_fail "The status does not show the data corruption"
echo "$status" | grep -q 'gave up on the lagging child' &&
	log_fail "The status shows the frozen mirror in place of the errors"
echo "$status" | grep -q \
	'not protected: gave up (copy stalled), the child needs a resilver' ||
	log_fail "The async mirror line does not show the stall"

status=$(zpool status -x $TESTPOOL 2>&1)
echo "$status" | grep -q 'resulting in data' ||
	log_fail "zpool status -x does not show the data corruption"

log_pass "Data errors are not hidden by a freeze that is no read error."
