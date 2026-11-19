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
# Copyright (c) 2026, Kyle Evans <kevans@FreeBSD.org>
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	The size and existing contents of an append-only file cannot be
#	changed through a descriptor opened with O_APPEND, the only way such
#	a file can be opened for writing.
#
# STRATEGY:
#	1. Create a file with random contents and make it append-only
#	2. Through an O_APPEND descriptor, try to shrink and extend it with
#	   ftruncate(2) and to punch a hole in it
#	3. Verify that each attempt fails with EPERM and that the size and
#	   contents are unchanged
#

verify_runnable "global"

typeset file=$TESTDIR/append_file

function set_append
{
	if is_freebsd ; then
		chflags sappnd $1
	else
		chattr +a $1
	fi
}

function clear_append
{
	if is_freebsd ; then
		chflags nosappnd $1
	else
		chattr -a $1
	fi
}

function cleanup
{
	[[ -e $file ]] && log_must clear_append $file
	log_must rm -f $file
}

function check_unchanged
{
	log_must [ "$(stat_size $file)" = "$size" ]
	log_must [ "$(xxh128digest $file)" = "$hash" ]
}

log_onexit cleanup

log_assert "Append-only file data cannot be changed through O_APPEND"

log_must dd if=/dev/urandom of=$file bs=128k count=2
log_must sync_pool $TESTPOOL
log_must set_append $file
typeset size=$(stat_size $file)
typeset hash=$(xxh128digest $file)

log_mustnot_expect "Operation not permitted" \
    append_only_ops ftruncate $file 0
check_unchanged
log_mustnot_expect "Operation not permitted" \
    append_only_ops ftruncate $file $((size * 2))
check_unchanged
log_mustnot_expect "Operation not permitted" \
    append_only_ops punch $file 0 131072
check_unchanged

log_pass "Append-only file data cannot be changed through O_APPEND"
