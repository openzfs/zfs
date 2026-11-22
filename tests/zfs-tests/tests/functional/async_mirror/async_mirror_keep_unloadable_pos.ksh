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
#	The choice to keep one copy erases the other copy only after the
#	kept copy has loaded.  When the kept copy cannot be loaded, the
#	import fails and the other copy keeps its uberblocks, so the
#	operator can still choose it.
#
# STRATEGY:
#	1. Make two copies written separately, as the operator keep test does:
#	   the leader has files b, the lagging child alone has files c.
#	2. Save the lagging child, and damage every copy of the root block
#	   of its newest uberblock.
#	3. Import with --async-mirror-keep and the guid of the lagging
#	   child.  Verify that the import fails, and that the leader still
#	   has its newest uberblock.
#	4. Put the saved lagging child back, and import with the same
#	   choice.  Verify files a and c, and that files b are gone.
#

verify_runnable "global"

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	rm -f $SAVED
	cleanup_async_mirror
}

#
# Print "offset length" in 512-byte sectors of each copy of the root block
# of the newest uberblock of a vdev file.
#
function newest_root_dvas # file
{
	zdb -lu $1 | awk '
	    $1 == "txg" && $2 == "=" { txg = $3 }
	    $1 == "bp" && $2 == "=" && txg > best { best = txg; bp = $0 }
	END { print bp }' |
	    grep -o 'DVA\[[0-9]\]=<[0-9a-f]*:[0-9a-f]*:[0-9a-f]*>' |
	    while IFS='<:>' read _ _ off len; do
		echo $(( (0x$off + 4194304) / 512 )) $(( 0x$len / 512 ))
	done
}

log_onexit cleanup

SAVED=$TEST_BASE_DIR/async_mirror_lagging.saved

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
log_must wait_async_mirror_idle
async_mirror_export_behind b 2
async_mirror_suspend_copy 0
async_mirror_import_lagging
async_mirror_write_files c 2
typeset digests="$(async_mirror_digests a 2)
$(async_mirror_digests c 2)"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader
typeset -i leader=$(get_ring_max_txg $ASYNC_MIRROR_LEADER)
typeset guid=$(get_leaf_guid $ASYNC_MIRROR_LAGGING)

log_must cp --sparse=always $ASYNC_MIRROR_LAGGING $SAVED
typeset dvas="$(newest_root_dvas $ASYNC_MIRROR_LAGGING)"
[[ -n "$dvas" ]] || log_fail "No root block of the lagging child"
echo "$dvas" | while read offset length; do
	log_must dd if=/dev/zero of=$ASYNC_MIRROR_LAGGING bs=512 \
	    count=$length seek=$offset conv=notrunc
done

log_mustnot zpool import -d $ASYNC_MIRROR_DIR --async-mirror-keep=$guid \
    $TESTPOOL
log_mustnot poolexists $TESTPOOL
log_must test $(get_ring_max_txg $ASYNC_MIRROR_LEADER) -eq $leader

log_must mv $SAVED $ASYNC_MIRROR_LAGGING
log_must zpool import -d $ASYNC_MIRROR_DIR --async-mirror-keep=$guid \
    $TESTPOOL
log_must test "$digests" == "$(async_mirror_digests a 2)
$(async_mirror_digests c 2)"
log_mustnot test -e /$TESTPOOL/b.0
log_must wait_async_mirror_resilvered
log_must verify_pool $TESTPOOL

log_pass "A keep that cannot load the kept copy erases nothing."
