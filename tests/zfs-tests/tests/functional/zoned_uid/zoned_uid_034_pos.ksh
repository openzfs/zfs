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
#	Renaming a subtree moves every local zoned_uid assignment.
#
# STRATEGY:
#	1. Create a root and child delegated to different UIDs.
#	2. Rename the root and recreate the old dataset names.
#	3. Confirm the child owner sees only the renamed child.
#

verify_runnable "global"

typeset root="$TESTPOOL/$TESTFS/deleg_root"
typeset moved="$TESTPOOL/$TESTFS/deleg_moved"
typeset child="$root/child"
typeset moved_child="$moved/child"

function cleanup
{
	zfs destroy -rf "$root" 2>/dev/null
	zfs destroy -rf "$moved" 2>/dev/null
}

log_assert "Dataset rename moves descendant zoned_uid assignments"
log_onexit cleanup

log_must zfs create "$root"
log_must set_zoned_uid "$root" "$ZONED_TEST_UID"
log_must zfs create "$child"
log_must set_zoned_uid "$child" "$ZONED_OTHER_UID"

log_must zfs rename "$root" "$moved"
log_must zfs create "$root"
log_must zfs create "$child"

log_mustnot run_in_userns "$ZONED_OTHER_UID" list "$child"
log_must run_in_userns "$ZONED_OTHER_UID" list "$moved_child"

log_pass "Dataset rename moves descendant zoned_uid assignments"
