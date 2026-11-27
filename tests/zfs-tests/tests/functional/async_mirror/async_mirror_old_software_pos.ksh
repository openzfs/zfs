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
#	Software without the async_mirror feature cannot import a pool whose
#	lagging child is behind, not even read-only.  Such software has no
#	read fence, and a read-only import does not load the DTLs, so a
#	mirror read can go to either child.  A block born after the txg of
#	the lagging child is not on that child: with a checksum, the read
#	counts a checksum error against a healthy disk; with checksum=off,
#	it returns wrong data with no error.  So the feature is not read-only
#	compatible, and it is active while the child lags.
#
#	This software cannot ignore the mark of the lagging child, so the
#	test checks the pool on disk with zdb.  Software without a feature
#	refuses the import when the feature is active in features_for_read.
#	async_mirror_readonly_import_fence_pos shows that
#	this software, with the fence, reads
#	the same pool correctly.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror, and a dataset with
#	   checksum=off.  Turn the async mirror on, write file a, and wait until
#	   the lagging child has caught up.
#	2. Stop the copy.  Write file b to the pool and file c to the
#	   dataset.  Verify that their data blocks have other bytes on the
#	   lagging child than on the leader.  Export the pool.
#	3. Verify that the pool needs the async_mirror feature for a read.
#	4. Import the pool, restart the copy, and turn the async mirror off.  Wait
#	   until the feature is enabled, and export the pool.  Verify that
#	   the pool no longer needs the feature for a read.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

#
# Succeed if the exported test pool has an active async_mirror feature in
# features_for_read.  Software without that feature refuses the import.
#
function async_mirror_needed_for_read
{
	typeset obj

	obj=$(zdb -e -p $ASYNC_MIRROR_DIR -dddd $TESTPOOL 1 | \
	    awk '$1 == "features_for_read" { print $3; exit }')
	[[ -n "$obj" ]] || return 1
	zdb -e -p $ASYNC_MIRROR_DIR -dddd $TESTPOOL $obj | \
	    awk '$1 ~ /async_mirror/ && $2 == "=" && $3 > 0 { f = 1 }
	    END { exit !f }'
}

#
# Print the number of blocks in a list whose bytes on the lagging child
# differ from those on the leader.
#
# $1 the list: one block per line, offset and length in 512-byte units
#
function async_mirror_count_stale # list
{
	typeset -i n=0
	typeset offset length lead lag

	while read offset length; do
		[[ -n "$offset" ]] || continue
		lead=$(dd if=$ASYNC_MIRROR_LEADER bs=512 skip=$offset \
		    count=$length 2>/dev/null | cksum)
		lag=$(dd if=$ASYNC_MIRROR_LAGGING bs=512 skip=$offset \
		    count=$length 2>/dev/null | cksum)
		[[ "$lead" == "$lag" ]] || (( n++ ))
	done <<< "$1"
	echo $n
}

#
# Print the data blocks of a file on the lagging child: offset and length
# in 512-byte units.  The leader has each block at the same offset.
#
# $1 path of the file
#
function async_mirror_lagging_blocks # file
{
	list_file_blocks $1 | awk -v p=$ASYNC_MIRROR_LAGGING \
	    '$1 == "L0" && $2 == p { print $3, $4 }'
}

async_mirror_create_pool
log_must zfs create -o checksum=off $TESTPOOL/nock
async_mirror_enable
async_mirror_write_files a 1
log_must wait_async_mirror_idle

async_mirror_suspend_copy 1
async_mirror_write /$TESTPOOL/b 64
async_mirror_write /$TESTPOOL/nock/c 64
sync_pool $TESTPOOL
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
typeset blocks_b="$(async_mirror_lagging_blocks /$TESTPOOL/b)"
typeset blocks_c="$(async_mirror_lagging_blocks /$TESTPOOL/nock/c)"
log_must test -n "$blocks_b"
log_must test -n "$blocks_c"
typeset -i stale_b=$(async_mirror_count_stale "$blocks_b")
typeset -i stale_c=$(async_mirror_count_stale "$blocks_c")
log_note "blocks not on the lagging child: $stale_b of b, $stale_c of c"
log_must test $stale_b -gt 0
log_must test $stale_c -gt 0
log_must zpool export $TESTPOOL

log_must async_mirror_needed_for_read

async_mirror_import
async_mirror_suspend_copy 0
async_mirror_disable
log_must wait_async_mirror_feature enabled
log_must zpool export $TESTPOOL
log_mustnot async_mirror_needed_for_read
async_mirror_import

log_pass "Software without the feature cannot import a pool that lags."
