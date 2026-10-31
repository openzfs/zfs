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
#	A zoned_uid rename requires create and mount permissions on the
#	destination parent.
#
# STRATEGY:
#	1. Create source and destination datasets in one delegation root.
#	2. Grant destination permissions and confirm missing source mount
#	   permission denies the rename.
#	3. Grant source permissions and confirm missing destination mount
#	   and create permissions each deny the rename.
#	4. Grant all required permissions and confirm the rename succeeds.
#

verify_runnable "global"

typeset root="$TESTPOOL/$TESTFS/deleg_root"
typeset src_no_mount="$root/src_no_mount"
typeset src="$root/src"
typeset dst="$root/dst"

function cleanup
{
	zfs destroy -rf "$root" 2>/dev/null
}

log_assert "zoned_uid rename checks destination parent permissions"
log_onexit cleanup

log_must zfs create -o mountpoint=none "$root"
log_must set_zoned_uid "$root" "$ZONED_TEST_UID"
log_must zfs create "$src_no_mount"
log_must zfs create "$src"
log_must zfs create "$dst"

log_must grant_deleg "$src_no_mount" "$ZONED_TEST_UID" rename
log_must grant_deleg "$dst" "$ZONED_TEST_UID" create,mount
log_mustnot run_in_userns "$ZONED_TEST_UID" rename "$src_no_mount" \
    "$dst/no_source_mount"
log_must datasetexists "$src_no_mount"

log_must grant_deleg "$src" "$ZONED_TEST_UID" rename,mount
log_must revoke_deleg "$dst" "$ZONED_TEST_UID" mount
log_mustnot run_in_userns "$ZONED_TEST_UID" rename "$src" "$dst/moved"
log_must datasetexists "$src"

log_must grant_deleg "$dst" "$ZONED_TEST_UID" mount
log_must revoke_deleg "$dst" "$ZONED_TEST_UID" create
log_mustnot run_in_userns "$ZONED_TEST_UID" rename "$src" "$dst/moved"
log_must datasetexists "$src"

log_must grant_deleg "$dst" "$ZONED_TEST_UID" create
log_must run_in_userns "$ZONED_TEST_UID" rename "$src" "$dst/moved"
log_must datasetexists "$dst/moved"

log_pass "zoned_uid rename checks destination parent permissions"
