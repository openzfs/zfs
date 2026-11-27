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
# DESCRIPTION:
#	A deadlist entry whose bpobj no longer exists is treated as lost:
#	snapshots of the dataset can still be taken and destroyed, blocks
#	can still be freed, and the dataset can be destroyed.  The pool
#	doesn't panic, the lost bpobj is reported, and the space its blocks
#	use ends up counted as leaked.
#
# STRATEGY:
#	1. Create a pool with file systems a and b.  In each, write two
#	   files, snapshot @s1 and remove one file, so that the head's
#	   deadlist has an entry with a bpobj of its own.
#	2. Export the pool and free that bpobj in each with
#	   "zhack deadlist damage", keeping the entries.  The entries name
#	   an object number that is never allocated, rather than the freed
#	   one, which the pool may reuse.  Import it.
#	3. a: snapshot it (the head's deadlist is cloned, the path in
#	   #19879), check the report and that zdb shows the entry as missing
#	   in a@s2, which now owns the damaged deadlist.  Destroy a@s1 (its
#	   blocks move into a@s2's deadlist), then a and its snapshots.
#	4. b: remove the other file (its blocks go to the lost entry and
#	   are leaked with it), check the report and that zdb still shows
#	   the entry as missing, and destroy b and its snapshot.
#	5. Once the frees are done, check that the pool counts leaked space,
#	   is healthy, and exports and imports.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

POOL=dlbpobj_$$
DIR=$(mktemp -d $TEST_BASE_DIR/dlbpobj.XXXXXX) || log_fail "mktemp -d failed"
VDEV=$DIR/vdev
# Not the system cache file: a system without the fix would import the
# damaged pool at boot.
CACHE=$DIR/cache

function cleanup
{
	poolexists $POOL && destroy_pool $POOL
	rm -f $VDEV $CACHE
	[[ -d $DIR ]] && rmdir $DIR
}

#
# Check that the kernel reported the lost bpobj of deadlist $1 entry $2
# (bpobj $3) and went on.
#
function check_report # dlobj mintxg obj
{
	typeset msg="pool $POOL: deadlist $1 entry $2: bpobj $3 is lost"
	typeset line=$(kstat dbgmsg | grep -F "$msg (error " | tail -1)
	[[ $line == *"treating it as empty"* ]] || \
	    log_fail "no report of the lost bpobj: $msg"
	log_note "reported: ${line#*\): }"
}

#
# Print the bpobj that zdb lists for dataset $1's deadlist entry $2, or
# "missing".
#
function zdb_entry # dataset mintxg
{
	zdb -U $CACHE -dddd $1 | awk -v t=$2 '
	    /Deadlist:/ { d = 1 }
	    d && $1 == "mintxg" && $2 == t { print $NF; exit }'
}

log_onexit cleanup

log_assert "A deadlist entry whose bpobj is missing doesn't panic the pool"

log_must truncate -s $MINVDEVSIZE $VDEV
log_must zpool create -o cachefile=$CACHE -O compression=off $POOL $VDEV
for fs in a b; do
	log_must zfs create $POOL/$fs
	log_must dd if=/dev/urandom of=/$POOL/$fs/file1 bs=128k count=8
	log_must dd if=/dev/urandom of=/$POOL/$fs/file2 bs=128k count=8
	log_must sync_pool $POOL
	log_must zfs snapshot $POOL/$fs@s1
	log_must rm /$POOL/$fs/file1
	log_must sync_pool $POOL
done

log_must zpool export $POOL
typeset -A dl mintxg obj
# An object number far beyond any the test allocates.
lost=4294967296
for fs in a b; do
	out=$(zhack -d $DIR deadlist damage -o $lost $POOL/$fs)
	(($? == 0)) || log_fail "zhack deadlist damage $POOL/$fs failed"
	log_note "$fs: $out"
	set -A w $out
	[[ ${w[0]} == freed && ${w[1]} == bpobj ]] || \
	    log_fail "unexpected zhack output: $out"
	obj[$fs]=$lost
	dl[$fs]=${w[5]}
	mintxg[$fs]=${w[7]}
done
log_must zpool import -o cachefile=$CACHE -d $DIR $POOL

# a: the head's deadlist is cloned for a new snapshot.
log_must zfs snapshot $POOL/a@s2
log_must sync_pool $POOL
check_report ${dl[a]} ${mintxg[a]} ${obj[a]}
# The snapshot took the head's deadlist; the head has a new one.
entry=$(zdb_entry $POOL/a@s2 ${mintxg[a]})
[[ $entry == missing ]] || \
    log_fail "zdb: a@s2's entry ${mintxg[a]} is '$entry', expected missing"
log_must zfs destroy $POOL/a@s1
log_must zfs destroy -r $POOL/a
log_must sync_pool $POOL
log_mustnot datasetexists $POOL/a

# b: blocks born before @s1 are added to the damaged entry.
log_must rm /$POOL/b/file2
log_must sync_pool $POOL
check_report ${dl[b]} ${mintxg[b]} ${obj[b]}
entry=$(zdb_entry $POOL/b ${mintxg[b]})
[[ $entry == missing ]] || \
    log_fail "zdb: b's entry ${mintxg[b]} is '$entry', expected missing"
log_must zfs destroy -r $POOL/b
log_must sync_pool $POOL
log_mustnot datasetexists $POOL/b

# The lost entries' space can't be freed: it is counted as leaked.
log_must zpool wait -t free $POOL
log_must sync_pool $POOL
leaked=$(get_pool_prop leaked $POOL)
log_note "leaked: $leaked"
[[ $leaked == +([0-9]) ]] && ((leaked > 0)) || \
    log_fail "expected leaked space, got '$leaked'"
log_must eval "zpool status -x $POOL | grep -q 'is healthy'"
log_must zpool export $POOL
log_must zpool import -o cachefile=$CACHE -d $DIR $POOL
log_must eval "zpool status -x $POOL | grep -q 'is healthy'"

log_pass "A deadlist entry whose bpobj is missing doesn't panic the pool"
