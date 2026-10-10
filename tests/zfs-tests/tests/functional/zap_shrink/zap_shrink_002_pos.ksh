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
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# ZAP shrinking must cope with the pointer table growing while
# zap_shrink() has dropped the ZAP lock to upgrade it.
#
# STRATEGY:
# 1. Create enough files in a directory to make its ZAP a fat ZAP with
#    many leaves.
# 2. Set zap_shrink_test_grow_ptrtbl, so that the next few times
#    zap_shrink() upgrades its lock to collapse two leaves, the pointer
#    table is grown in the window where the lock may be dropped.
# 3. Remove all files, so the ZAP shrinks, and check with zdb, which walks
#    the pointer table and every leaf it points to, that the pointer table
#    did grow.
# 4. Create and remove the same files again, checking that every name
#    can be found, and that the ZAP shrinks back to a single leaf.
#

verify_runnable "global"

DIR=growdir
NR_FILES=10000
BATCH=1000
NR_GROW=2
CWD=$PWD
ZDBOUT=$TEST_BASE_DIR/zap_shrink_002.zdb

function cleanup
{
	cd $CWD
	log_must set_tunable32 ZAP_SHRINK_TEST_GROW_PTRTBL 0
	rm -rf $TESTDIR/$DIR $ZDBOUT
}

# Sets zt_shift and nleafs. zdb walks the pointer table and every leaf.
function zap_stats
{
	log_must eval "zdb -v -O $TESTPOOL/$TESTFS $DIR > $ZDBOUT"
	zt_shift=$(awk -F: '/zt_shift:/ { print $2 + 0; exit }' $ZDBOUT)
	nleafs=$(awk -F: '/Leaf blocks:/ { print $2 + 0; exit }' $ZDBOUT)
}

function create_files
{
	cd $TESTDIR/$DIR
	for i in $(seq $(($NR_FILES/$BATCH))); do
		log_must touch $(seq $((($i-1)*$BATCH+1)) $(($i*$BATCH)))
	done
	cd $CWD
}

function remove_files
{
	cd $TESTDIR/$DIR
	for i in $(seq $(($NR_FILES/$BATCH))); do
		log_must rm $(seq $((($i-1)*$BATCH+1)) $(($i*$BATCH)))
	done
	cd $CWD
}

log_onexit cleanup

log_assert "ZAP shrinking copes with the pointer table growing while" \
	"its lock is dropped."

log_must mkdir $TESTDIR/$DIR
create_files
sync_pool $TESTPOOL
log_must test $NR_FILES -eq $(ls -U $TESTDIR/$DIR | wc -l)

zap_stats
shift_before=$zt_shift
log_note "before: zt_shift $zt_shift, $nleafs leaf blocks"
log_must test $nleafs -gt 1

log_must set_tunable32 ZAP_SHRINK_TEST_GROW_PTRTBL $NR_GROW
remove_files
sync_pool $TESTPOOL
log_must test 0 -eq $(get_tunable ZAP_SHRINK_TEST_GROW_PTRTBL)
log_must test 0 -eq $(ls -U $TESTDIR/$DIR | wc -l)

zap_stats
log_note "after: zt_shift $zt_shift, $nleafs leaf blocks"
# The first two growths of a ZAP's pointer table complete at once; later
# ones copy it incrementally, so only check that it grew at all.
log_must test $zt_shift -gt $shift_before

# Every name must be found again, both by readdir and by lookup.
create_files
sync_pool $TESTPOOL
log_must test $NR_FILES -eq $(ls -U $TESTDIR/$DIR | wc -l)
log_must test $NR_FILES -eq $(cd $TESTDIR/$DIR && seq $NR_FILES | \
    xargs ls -d | wc -l)
remove_files
sync_pool $TESTPOOL
log_must test 0 -eq $(ls -U $TESTDIR/$DIR | wc -l)

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

zap_stats
log_must test 1 -eq $nleafs

log_pass "ZAP shrinking copes with the pointer table growing while" \
	"its lock is dropped."
