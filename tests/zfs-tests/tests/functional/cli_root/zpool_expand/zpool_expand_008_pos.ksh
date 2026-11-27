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
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# With autoexpand=on, a reopen that finds larger devices grows the
# top-level vdev's usable size, not just its recorded size (issue #12501).
#
# STRATEGY:
# 1) Create a pool with a normal mirror and a special mirror, autoexpand off
# 2) Grow the special mirror's members and reopen: EXPANDSZ shows the space
# 3) Set autoexpand=on and reopen the pool again
# 4) The special mirror's SIZE must grow, and EXPANDSZ must be 0
# 5) The same must hold after an export and import
#
# A reopen with autoexpand=on records the larger size of a top-level vdev
# (vdev_asize), which is what EXPANDSZ is computed from.  The metaslabs for
# the new space are only added by vdev_expand(), so when nothing called it
# after the reopen, EXPANDSZ went to 0 and SIZE stayed where it was, and
# "zpool online -e" no longer had any size change to act on.
#

verify_runnable "global"

typeset -r SMALLSZ=$((512 * 1024 * 1024))
typeset -r BIGSZ=$((1024 * 1024 * 1024))
typeset -r DIR=$TEST_BASE_DIR/expand-reopen
typeset -r SPECIAL=mirror-1

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -f $DIR/n1 $DIR/n2 $DIR/s1 $DIR/s2
	rmdir $DIR 2>/dev/null
}

# A top-level vdev's SIZE or EXPANDSZ, in bytes ("-" is 0).
function tvd_prop # prop
{
	zpool list -Hpv -o name,$1 $TESTPOOL1 | \
	    awk -v t=$SPECIAL '$1 == t { print ($2 == "-" ? 0 : $2) }'
}

log_onexit cleanup

log_assert "a reopen with autoexpand=on grows a special mirror's usable size"

log_must mkdir -p $DIR
for f in n1 n2 s1 s2; do
	log_must truncate -s $SMALLSZ $DIR/$f
done
log_must zpool create -o autoexpand=off $TESTPOOL1 \
    mirror $DIR/n1 $DIR/n2 special mirror $DIR/s1 $DIR/s2

typeset -i size0=$(tvd_prop size)
log_note "$SPECIAL size $size0"

log_must truncate -s $BIGSZ $DIR/s1 $DIR/s2
log_must zpool reopen $TESTPOOL1
typeset -i esize=$(tvd_prop expandsize)
log_note "after growing the members: size $(tvd_prop size) expandsize $esize"
if ((esize == 0)); then
	log_fail "EXPANDSZ is 0 after growing the members with autoexpand=off"
fi

log_must zpool set autoexpand=on $TESTPOOL1
log_must zpool reopen $TESTPOOL1
log_must zpool sync $TESTPOOL1

# The configuration update that adds the metaslabs runs asynchronously.
typeset -i size1=$size0
typeset -i i=0
while ((i < 20)); do
	size1=$(tvd_prop size)
	((size1 > size0)) && break
	sleep 0.5
	((i = i + 1))
done
esize=$(tvd_prop expandsize)
log_note "after reopen with autoexpand=on: size $size1 expandsize $esize"
if ((size1 <= size0)); then
	log_fail "$SPECIAL did not grow: size $size1 (was $size0)," \
	    "expandsize $esize"
fi
if ((esize != 0)); then
	log_fail "$SPECIAL grew to $size1 but still has expandsize $esize"
fi

log_must zpool export $TESTPOOL1
log_must zpool import -d $DIR $TESTPOOL1
typeset -i size2=$(tvd_prop size)
log_note "after export and import: size $size2"
if ((size2 != size1)); then
	log_fail "$SPECIAL size $size2 after import, $size1 before"
fi
log_must check_state $TESTPOOL1 "" "ONLINE"

log_pass "a reopen with autoexpand=on grows a special mirror's usable size"
