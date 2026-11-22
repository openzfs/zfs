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
#	While the async mirror is on, a scrub finds damage on the lagging child in a
#	block that the child already holds, and repairs it.  A block born at
#	or before the txg of the lagging child has the same bytes on both
#	children, so a read of it on the lagging child is valid, and a
#	repair write of it does not write the child out of txg order.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Write
#	   files a and wait until the lagging child has caught up.  Write
#	   files c and wait again, so that the lagging child is well past
#	   the txgs of a.
#	2. Write random bytes over the data blocks of a.0 on the lagging
#	   child only.
#	3. Scrub.  Verify that the scrub counts checksum errors on the
#	   lagging child, and none on the leader.
#	4. Clear the errors and scrub again.  Verify that the scrub finds no
#	   error on either child.
#	5. Wait until the lagging child has caught up, export, and import
#	   the lagging child alone.  Verify a.0 and a scrub with no error.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

#
# Write random bytes over the data blocks of a file on the lagging child
# only.  The pool must be imported.
#
# $1 path of the file
#
function async_mirror_corrupt_lagging # file
{
	typeset -i n=0
	typeset level path offset length

	list_file_blocks $1 | while read level path offset length; do
		[[ $level == "L0" && $path == "$ASYNC_MIRROR_LAGGING" ]] ||
		    continue
		log_must dd if=/dev/urandom of=$path bs=512 count=$length \
		    seek=$offset conv=notrunc
		(( n++ ))
	done
	(( n > 0 )) || log_fail "No data block of $1 on the lagging child"
	sync
}

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
typeset -i a_txg=$(get_async_mirror_pool_txg)
log_must wait_async_mirror_idle
async_mirror_write_files c 2
log_must wait_async_mirror_idle
typeset -i applied=$(get_async_mirror_stat applied_txg)
log_note "files a in txg $a_txg or before, lagging child at txg $applied"
log_must test $applied -gt $a_txg
typeset digests="$(async_mirror_digests a 2)"

async_mirror_corrupt_lagging /$TESTPOOL/a.0

log_must zpool scrub -w $TESTPOOL
typeset -i lead_errs=$(async_mirror_cksum_errors $ASYNC_MIRROR_LEADER)
typeset -i lag_errs=$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)
log_note "first scrub: leader $lead_errs, lagging child $lag_errs" \
    "checksum errors"
log_must test $lag_errs -gt 0
log_must test $lead_errs -eq 0
log_must test "$digests" == "$(async_mirror_digests a 2)"

log_must zpool clear $TESTPOOL
log_must zpool scrub -w $TESTPOOL
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LEADER)" -eq 0
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" -eq 0

sync_pool $TESTPOOL
log_must wait_async_mirror_idle
log_must zpool export $TESTPOOL
async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests a 2)"
log_must zpool scrub -w $TESTPOOL
log_must test "$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)" -eq 0
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "A scrub finds and repairs damage on the lagging child."
