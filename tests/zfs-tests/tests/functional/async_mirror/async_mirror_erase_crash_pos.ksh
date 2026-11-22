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
#	A crash while an import erases the copy that the operator does not
#	keep leaves a pool that the next import handles safely.  The erase
#	of the other child's uberblocks runs in two flushed phases: first
#	the uberblocks at or below the fork txg of the kept copy, which look
#	like points of the kept history, then the others.  A crash after the
#	first phase leaves only uberblocks above the fork, and the import is
#	refused again.  A crash after the second phase, before the first
#	txg, leaves no uberblock, and a plain import resilvers that child in
#	full.
#
# STRATEGY:
#	1. Make two copies written separately, as
#	   async_mirror_operator_keeps_lagging_copy_after_import_refusal_pos does:
#	   the leader has files b, and the lagging child, imported alone at
#	   its newest txg F, has files c.
#	2. Erase the uberblocks of the leader at or below F, as the first
#	   phase of the erase does when the lagging child's copy is kept.
#	   Verify that a read-write import is refused as copies written
#	   separately.
#	3. Erase every other uberblock of the leader, as the second phase
#	   does.  Import with no choice.  Verify files a and c, that files b
#	   are gone, and wait for the resilver.
#	4. Import the leader alone, read-only.  Verify files a and c, and
#	   that it has no errors.  Import both and verify the pool.
#

verify_runnable "global"

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	[[ -e $ASYNC_MIRROR_LAGGING_HIDDEN ]] && async_mirror_unhide_lagging
	cleanup_async_mirror
}

#
# Write zeros over each uberblock slot of a vdev file, in all four labels,
# that holds a txg above low and at or below high.  The pool must not be
# imported.
#
function erase_ring_txgs # file low high
{
	typeset file=$1
	typeset -i low=$2 high=$3
	typeset -i ashift shift count size slot label txg
	typeset -a offs

	ashift=$(zdb -l $file | awk '$1 == "ashift:" { print $2; exit }')
	(( shift = ashift < 10 ? 10 : (ashift > 13 ? 13 : ashift) ))
	(( count = (128 * 1024) >> shift ))
	(( size = $(stat_size $file) / (256 * 1024) * (256 * 1024) ))
	set -A offs 0 $((256 * 1024)) $((size - 512 * 1024)) \
	    $((size - 256 * 1024))
	for txg in $(get_ring_txgs $file); do
		(( txg > low && txg <= high )) || continue
		(( slot = txg % count ))
		for label in 0 1 2 3; do
			log_must dd if=/dev/zero of=$file bs=512 conv=notrunc \
			    count=$(( (1 << shift) / 512 )) \
			    seek=$(( (offs[label] + 128 * 1024 + \
			    (slot << shift)) / 512 ))
		done
	done
}

log_onexit cleanup

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files a 2
log_must wait_async_mirror_idle
async_mirror_export_behind b 2
async_mirror_suspend_copy 0
typeset -i fork=$(get_ring_max_txg $ASYNC_MIRROR_LAGGING)
typeset -i leader=$(get_ring_max_txg $ASYNC_MIRROR_LEADER)
log_must test $leader -gt $fork

async_mirror_import_lagging
async_mirror_write_files c 2
typeset digests="$(async_mirror_digests a 2)
$(async_mirror_digests c 2)"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

erase_ring_txgs $ASYNC_MIRROR_LEADER 0 $fork
log_must test $(count_ring_txgs $ASYNC_MIRROR_LEADER 0 $fork) -eq 0
log_must test $(count_ring_txgs $ASYNC_MIRROR_LEADER $fork $leader) -gt 0
typeset out
out=$(zpool import -d $ASYNC_MIRROR_DIR $TESTPOOL 2>&1) &&
    log_fail "The import accepted a copy erased in part"
log_note "$out"
echo "$out" | grep -q "written separately" ||
    log_fail "The import failed with another error"
log_mustnot poolexists $TESTPOOL

erase_ring_txgs $ASYNC_MIRROR_LEADER 0 $leader
log_must test -z "$(get_ring_txgs $ASYNC_MIRROR_LEADER)"
async_mirror_import
log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests c 2)"
log_mustnot test -e /$TESTPOOL/b.0
log_must wait_async_mirror_resilvered
log_must zpool export $TESTPOOL

log_must mv $ASYNC_MIRROR_LAGGING $ASYNC_MIRROR_LAGGING_HIDDEN
log_must zpool import -o readonly=on -d $ASYNC_MIRROR_DIR $TESTPOOL
log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests c 2)"
log_must check_pool_status $TESTPOOL errors "No known data errors"
log_must zpool export $TESTPOOL
async_mirror_unhide_lagging

async_mirror_import
log_must verify_pool $TESTPOOL

log_pass "A crash during the erase of the discarded copy is safe."
