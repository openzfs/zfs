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
#	The most specific zoned_uid value controls dataset visibility.
#
# STRATEGY:
#	1. Delegate a root to one UID.
#	2. Set one child to zero and another child to a different UID.
#	3. Confirm the root owner cannot see either child.
#	4. Confirm the different UID can see its child.
#	5. Inherit the zero override and confirm ancestor access returns.
#	6. Check recursive discovery through foreign and zero-valued ancestors.
#

verify_runnable "global"

typeset root="$TESTPOOL/$TESTFS/deleg_root"
typeset disabled="$root/disabled"
typeset reassigned="$root/reassigned"
typeset listing

function cleanup
{
	zfs destroy -rf "$root" 2>/dev/null
}

log_assert "The most specific zoned_uid controls visibility"
log_onexit cleanup

log_must zfs create "$root"
log_must set_zoned_uid "$root" "$ZONED_TEST_UID"
log_must zfs create "$disabled"
log_must zfs create "$reassigned"
log_must set_zoned_uid "$disabled" 0
log_must set_zoned_uid "$reassigned" "$ZONED_OTHER_UID"

log_must run_in_userns "$ZONED_TEST_UID" list "$root"
log_mustnot run_in_userns "$ZONED_TEST_UID" list "$disabled"
log_mustnot run_in_userns "$ZONED_TEST_UID" list "$reassigned"
log_must run_in_userns "$ZONED_OTHER_UID" list "$reassigned"
listing=$(run_in_userns "$ZONED_OTHER_UID" list -H -o name -r "$TESTPOOL") ||
    log_fail "Cannot enumerate nested delegation"
log_must grep -Fx "$reassigned" <<< "$listing"
log_mustnot run_in_userns "$ZONED_OTHER_UID" create "$root/unauthorized"

log_must set_zoned_uid "$root" 0
listing=$(run_in_userns "$ZONED_OTHER_UID" list -H -o name -r "$TESTPOOL") ||
    log_fail "Cannot enumerate delegation below zero boundary"
log_must grep -Fx "$reassigned" <<< "$listing"
log_must set_zoned_uid "$root" "$ZONED_TEST_UID"

log_must zfs inherit zoned_uid "$disabled"
log_must run_in_userns "$ZONED_TEST_UID" list "$disabled"

log_pass "The most specific zoned_uid controls visibility"
