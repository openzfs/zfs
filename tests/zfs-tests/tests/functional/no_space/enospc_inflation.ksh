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
. $STF_SUITE/tests/functional/no_space/enospc.cfg

#
# DESCRIPTION:
# With spa_asize_inflation=1, writes that fill the pool leave the slop
# space that administrative operations and file removal may use: zfs set
# and rm still succeed.
#
# STRATEGY:
# 1. Create a pool on a 3 GiB file vdev, large enough for 1/32 of its
#    space to exceed half the slop space, and write two small files.
# 2. Set spa_asize_inflation to 1 and write a file until ENOSPC.
# 3. Restore spa_asize_inflation and sync the pool twice.
# 4. Set a user property and remove a file.
#

verify_runnable "global"

typeset vdev=$TEST_BASE_DIR/vdev_inflation
typeset inflation=$(get_tunable SPA_ASIZE_INFLATION)

function cleanup
{
	set_tunable32 SPA_ASIZE_INFLATION $inflation
	destroy_pool $TESTPOOL
	rm -rf $TESTDIR $vdev
}

# spa_get_slop_space() of a pool without an embedded log class
function slop_space # size
{
	typeset -li big=$(( $1 >> 5 ))
	typeset -li small=$(( $1 >> 1 ))

	(( big > 137438953472 )) && big=137438953472
	(( small > 134217728 )) && small=134217728
	(( big > small )) && echo $big || echo $small
}

log_onexit cleanup

log_assert "Writes leave the slop space with spa_asize_inflation=1"

log_must truncate -s 3g $vdev
log_must zpool create -f -o ashift=12 -O mountpoint=none \
    -O compression=off -O atime=off $TESTPOOL $vdev
log_must zfs create -o mountpoint=$TESTDIR $TESTPOOL/$TESTFS
log_must file_write -o create -f $TESTDIR/keep.file -b 1048576 -c 16 -d $DATA
log_must file_write -o create -f $TESTDIR/rm.file -b 1048576 -c 16 -d $DATA
sync_pool $TESTPOOL true
sync_pool $TESTPOOL true

typeset -li size=$(get_pool_prop size $TESTPOOL)
typeset -li slop=$(slop_space $size)
typeset -li limit=$(( $(get_prop used $TESTPOOL) + \
    $(get_prop available $TESTPOOL) ))

log_must set_tunable32 SPA_ASIZE_INFLATION 1
file_write -o create -f $TESTDIR/fill.file -b 1048576 -c 4096 -d $DATA
typeset -i ret=$?
log_must set_tunable32 SPA_ASIZE_INFLATION $inflation
(( ret != ENOSPC )) && log_fail "fill.file returned $ret rather than ENOSPC"
sync_pool $TESTPOOL true
sync_pool $TESTPOOL true

typeset -li over=$(( $(get_prop used $TESTPOOL) - limit ))
log_note "Writes stopped $(( over >> 20 )) MiB past the writes' limit;" \
    "the level of zfs set is $(( slop >> 21 )) MiB past it"

log_must zfs set user:inflation=1 $TESTPOOL/$TESTFS
log_must rm $TESTDIR/rm.file

log_pass "Writes leave the slop space with spa_asize_inflation=1"
