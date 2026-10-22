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

. $STF_SUITE/tests/functional/zoned_uid/zoned_uid_common.kshlib

#
# DESCRIPTION:
#	Received zoned_uid values remain authoritative after inherit -S.
#
# STRATEGY:
#	1. Receive a dataset carrying a nonzero zoned_uid value.
#	2. Override the received value locally with zero.
#	3. Revert to the received value with inherit -S.
#	4. Confirm the assigned UID can see the dataset again.
#	5. Verify root limits are protected and child limits remain delegatable.
#

verify_runnable "global"

typeset source="$TESTPOOL/$TESTFS/recv_source"
typeset received="$TESTPOOL/$TESTFS/recv_target"

function cleanup
{
	zfs destroy -rf "$source" 2>/dev/null
	zfs destroy -rf "$received" 2>/dev/null
}

log_assert "inherit -S restores received zoned_uid authority"
log_onexit cleanup

log_must zfs create -o mountpoint=none "$source"
log_must set_zoned_uid "$source" "$ZONED_TEST_UID"
log_must zfs snapshot "$source@send"
log_must eval "zfs send -p '$source@send' | zfs receive '$received'"

log_must run_in_userns "$ZONED_TEST_UID" list "$received"
log_must set_zoned_uid "$received" 0
log_mustnot run_in_userns "$ZONED_TEST_UID" list "$received"
log_must zfs inherit -S zoned_uid "$received"
log_must run_in_userns "$ZONED_TEST_UID" list "$received"

log_must grant_deleg "$received" "$ZONED_TEST_UID" \
    filesystem_limit,snapshot_limit,destroy,mount
log_must zfs set filesystem_limit=10 snapshot_limit=20 "$received"
log_must zfs create "$received/child"
for property in filesystem_limit snapshot_limit; do
	log_mustnot run_in_userns "$ZONED_TEST_UID" set "$property=none" \
	    "$received"
	log_must run_in_userns "$ZONED_TEST_UID" set "$property=5" \
	    "$received/child"
done
log_must run_in_userns "$ZONED_TEST_UID" destroy "$received/child"
log_must zfs destroy "$received@send"
log_mustnot run_in_userns "$ZONED_TEST_UID" destroy "$received"

log_pass "inherit -S restores received zoned_uid authority"
