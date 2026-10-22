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
# A sequential rebuild must not copy stale data to the new leaf from a
# leaf that is onlined while the rebuild runs.
#
# STRATEGY:
# 1. Create a two-way mirror, write file A, offline the second leaf
#    and write file B, which that leaf misses.
# 2. Disable the scrub after the rebuild, which would otherwise repair
#    the new leaf.
# 3. With the rebuild paused, attach a third leaf with 'zpool attach -s'
#    and online the second leaf.
# 4. Resume the rebuild and wait for it.
# 5. Move the other leaves away, import the pool read-only from the new
#    leaf alone, and verify A and B.
#

verify_runnable "global"

DIR=$TEST_BASE_DIR/rebuild_online_leaf
REF=$DIR/ref

function cleanup
{
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS \
	    $ORIG_SCAN_SUSPEND_PROGRESS
	log_must set_tunable32 REBUILD_SCRUB_ENABLED \
	    $ORIG_REBUILD_SCRUB_ENABLED
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -rf $DIR
}

function write_file # name
{
	log_must dd if=/dev/urandom of=$REF/$1 bs=1M count=32
	log_must cp $REF/$1 /$TESTPOOL1/$1
	sync_pool $TESTPOOL1
}

log_assert "A rebuild does not copy stale data from an onlined leaf"

ORIG_SCAN_SUSPEND_PROGRESS=$(get_tunable SCAN_SUSPEND_PROGRESS)
ORIG_REBUILD_SCRUB_ENABLED=$(get_tunable REBUILD_SCRUB_ENABLED)

log_onexit cleanup

# Each leaf in its own directory, so the pool can be imported from one.
log_must mkdir -p $DIR/d0 $DIR/d1 $DIR/d2 $DIR/away $REF
log_must truncate -s 512M $DIR/d0/f $DIR/d1/f $DIR/d2/f
log_must zpool create -f $TESTPOOL1 mirror $DIR/d0/f $DIR/d1/f
write_file A
log_must zpool offline $TESTPOOL1 $DIR/d1/f
write_file B

log_must set_tunable32 REBUILD_SCRUB_ENABLED 0
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool attach -s $TESTPOOL1 $DIR/d0/f $DIR/d2/f
log_must zpool online $TESTPOOL1 $DIR/d1/f
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must zpool wait -t resilver $TESTPOOL1
zpool status -v $TESTPOOL1
log_must zpool export $TESTPOOL1

# The import would open the other leaves by path, so move them away.
log_must mv $DIR/d0/f $DIR/away/d0
log_must mv $DIR/d1/f $DIR/away/d1
typeset bad=""
log_must zpool import -o readonly=on -d $DIR/d2 $TESTPOOL1
zpool status -v $TESTPOOL1
for f in A B; do
	cmp -s $REF/$f /$TESTPOOL1/$f || bad="$bad $f"
done
log_must zpool export $TESTPOOL1
log_must mv $DIR/away/d0 $DIR/d0/f
log_must mv $DIR/away/d1 $DIR/d1/f
log_must zpool import -d $DIR/d0 -d $DIR/d1 -d $DIR/d2 $TESTPOOL1
[[ -z "$bad" ]] || log_fail "the new leaf alone is missing data in:$bad"

log_pass "A rebuild does not copy stale data from an onlined leaf"
