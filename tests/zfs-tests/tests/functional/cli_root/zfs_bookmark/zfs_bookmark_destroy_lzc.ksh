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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# lzc_destroy_bookmarks() destroys case-insensitive aliases exactly once.
#
# STRATEGY:
# - Create case-sensitive and case-insensitive filesystems.
# - Destroy two distinct bookmarks from the case-sensitive filesystem.
# - Pass two aliases of one case-insensitive bookmark in a single request.
# - Verify each logical bookmark was destroyed exactly once.
#

verify_runnable "both"

typeset ROOT="$TESTPOOL/${TESTFS}_bookmark_destroy_lzc"
typeset SENSITIVE="$ROOT/sensitive"
typeset INSENSITIVE="$ROOT/insensitive"

function cleanup
{
	datasetexists "$ROOT" && destroy_dataset "$ROOT" "-r"
}

function bookmark_must_not_exist
{
	bkmarkexists "$1" && log_fail "Bookmark $1 exists"
	return 0
}

log_assert "lzc_destroy_bookmarks handles case-insensitive aliases"
log_onexit cleanup

log_must zfs create "$ROOT"
log_must zfs create "$SENSITIVE"
log_must zfs create -o casesensitivity=insensitive "$INSENSITIVE"
log_must zfs snapshot "$SENSITIVE@source"
log_must zfs snapshot "$INSENSITIVE@source"

log_must zfs bookmark "$SENSITIVE@source" "$SENSITIVE#mixed"
log_must zfs bookmark "$SENSITIVE@source" "$SENSITIVE#MiXeD"
log_must lzc_destroy_bookmarks "$SENSITIVE#mixed" "$SENSITIVE#MiXeD"
bookmark_must_not_exist "$SENSITIVE#mixed"
bookmark_must_not_exist "$SENSITIVE#MiXeD"

log_must zfs bookmark "$INSENSITIVE@source" "$INSENSITIVE#MiXeD"
log_must lzc_destroy_bookmarks "$INSENSITIVE#mixed" "$INSENSITIVE#MIXED"
bookmark_must_not_exist "$INSENSITIVE#MiXeD"

log_pass "lzc_destroy_bookmarks handles case-insensitive aliases"
