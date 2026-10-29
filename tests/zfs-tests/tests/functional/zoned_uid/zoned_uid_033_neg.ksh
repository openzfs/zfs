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
#	Import does not turn inherited zoned_uid values into independent
#	delegations.
#
# STRATEGY:
#	1. Create a delegated root with an inheriting child.
#	2. Export and import the pool.
#	3. Unload and reopen the pool, retaining visibility.
#	4. Clear the root delegation.
#	5. Confirm the former owner cannot see the child.
#

verify_runnable "global"

typeset root="$TESTPOOL/$TESTFS/deleg_root"
typeset child="$root/child"

function cleanup
{
	zfs destroy -rf "$root" 2>/dev/null
}

log_assert "Imported inheritance follows later zoned_uid revocation"
log_onexit cleanup

log_must zfs create "$root"
log_must set_zoned_uid "$root" "$ZONED_TEST_UID"
log_must zfs create "$child"

log_must zpool export "$TESTPOOL"
log_must zpool import "$TESTPOOL"
log_must run_in_userns "$ZONED_TEST_UID" list "$child"

log_must zfs unmount "$child"
log_must zfs unmount "$root"
log_must zfs unmount "$TESTPOOL/$TESTFS"
log_must zfs unmount "$TESTPOOL"
log_must zinject -u "$TESTPOOL"
log_must zfs list "$child"
log_must run_in_userns "$ZONED_TEST_UID" list "$child"

log_must set_zoned_uid "$root" 0
log_mustnot run_in_userns "$ZONED_TEST_UID" list "$child"

log_pass "Imported inheritance follows later zoned_uid revocation"
