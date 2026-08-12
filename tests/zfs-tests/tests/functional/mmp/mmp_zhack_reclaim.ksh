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
. $STF_SUITE/tests/functional/mmp/mmp.cfg
. $STF_SUITE/tests/functional/mmp/mmp.kshlib

#
# DESCRIPTION:
#	Verify `zhack mmp reclaim` recovers a pool whose configuration expects
#	mirror legs that went away with a failed host, and that it relaxes the
#	uberblock claim no further than that.
#
# STRATEGY:
#	1. A pool stranded by legs the configuration still expects is
#	   recovered, and the claim then accepts it on an ordinary import.
#	   The import is made as a THIRD hostid on purpose: zhack exports
#	   cleanly under its own hostid, so importing again as the same host
#	   would skip the activity check and never exercise the claim at all.
#	2. The claim is relaxed only for legs this host cannot open.  A pool
#	   held by a live host which shares a leg with us is still refused.
#	3. Legs of every top level vdev are still counted after a recovery.
#	4. A pool which does not use multihost is left alone, since no claim
#	   runs for it and an ordinary import already succeeds.
#	5. Legs of a log vdev are not touched, since the claim never counts
#	   them.
#	6. Members of a raidz vdev are not touched.  The claim requires
#	   parity+1 writes in aggregate rather than one per member, so an
#	   absent member does not raise the requirement and there is nothing
#	   for this tool to recover.
#

verify_runnable "both"

# $HOSTID3 comes from mmp.cfg.  A third identity is needed so a post-recovery
# import cannot take the exported-and-same-hostid shortcut past the activity
# check, which would leave the claim unexercised.
typeset HIDDEN_LEG=$TEST_BASE_DIR/mmp_zhack_hidden_leg
typeset PEER_DIR=$TEST_BASE_DIR/mmp_zhack_peer
typeset DBGMSG=/proc/spl/kstat/zfs/dbgmsg
typeset MMP_IMPORT_ERR=$TEST_BASE_DIR/mmp_zhack_import_err

function cleanup
{
	mmp_pool_destroy $MMP_POOL $MMP_DIR
	rm -rf $HIDDEN_LEG $PEER_DIR $MMP_IMPORT_ERR
	log_must mmp_clear_hostid
}

log_assert "zhack mmp reclaim recovers a stranded pool and relaxes no further"
log_onexit cleanup

#
# Leave the pool looking imported, then take on a different identity, so the
# next import runs an activity check and an uberblock claim as it would after
# the original host failed.
#
function crash_export_as_other_host
{
	log_must zpool export -F $MMP_POOL
	log_must mmp_clear_hostid
	log_must mmp_set_hostid $HOSTID2
}

#
# Become a third host.  Kept out of claim_reimport because log_must writes
# its SUCCESS lines to stdout, which would be captured along with the
# numbers by the command substitution below.
#
function become_third_host
{
	log_must mmp_clear_hostid
	log_must mmp_set_hostid $HOSTID3
}

#
# Import and echo the claim's own accounting, "req good".  Empty if the
# import failed.  Nothing here may write to stdout except the numbers.
#
function claim_reimport
{
	typeset re='.*req_writes=([0-9]+) issued_writes=[0-9]+'
	re="$re good_writes=([0-9]+).*"

	echo 0 >$DBGMSG

	zpool import -d $MMP_DIR $MMP_POOL >/dev/null 2>&1 || return

	grep -h 'mmp: claiming uberblock' $DBGMSG 2>/dev/null |
	    sed -nE "s/$re/\1 \2/p" |
	    tail -1
}

# 1. A stranded pool is recovered, and the claim accepts it afterwards
log_note "Verify a stranded pool is recovered and the claim then accepts it"
mmp_pool_create_simple $MMP_POOL $MMP_DIR
crash_export_as_other_host
log_must mv $MMP_DIR/vdev2 $HIDDEN_LEG

#
# The import has to say which of the two refusals this is.  It used to report
# the same "pool is imported on host" text used for a pool another host holds,
# which is wrong here: nothing holds this pool, the claim simply could not
# reach every leg the config expects.  Both directions matter, so the wrong
# message is asserted against as well as the right one.
#
log_mustnot eval "zpool import -d $MMP_DIR -f $MMP_POOL >$MMP_IMPORT_ERR 2>&1"
log_must grep -q "could not be written to a device" $MMP_IMPORT_ERR
log_mustnot grep -q "pool is imported on host" $MMP_IMPORT_ERR
log_must rm -f $MMP_IMPORT_ERR

log_must zhack -d $MMP_DIR mmp reclaim $MMP_POOL

#
# zhack exports the pool when it is done, so there is nothing to inspect
# until the pool is imported again.  That import is the point of the test.
#
become_third_host
typeset nums=$(claim_reimport)
if [[ "$nums" != "1 1" ]]; then
	log_fail "claim after recovery counted '$nums', expected '1 1'"
fi
log_note "claim after recovery required 1 write and got 1"
log_must check_state $MMP_POOL "" "DEGRADED"

#
# The leg has to be left OFFLINE specifically.  A faulted leg would also read
# as DEGRADED and would also satisfy the claim, but only an offline one has
# "zpool online" as its documented inverse.
#
if ! zpool status $MMP_POOL | grep -q OFFLINE; then
	log_fail "the absent leg was not left offline after recovery"
fi
mmp_pool_destroy $MMP_POOL $MMP_DIR
log_must rm -f $HIDDEN_LEG

# 2. A pool held by a live host which shares a leg with us is still refused
log_note "Verify a live host sharing a leg is still refused"
mmp_pool_create_simple $MMP_POOL $MMP_DIR
log_must zpool export $MMP_POOL

#
# The peer keeps both legs.  We keep a link to one of them, so we cannot open
# the other and the relaxed claim has something to forgive, while the peer's
# multihost writes still reach a leg we can read.
#
log_must mkdir -p $PEER_DIR
log_must mv $MMP_DIR/vdev1 $PEER_DIR/vdev1
log_must mv $MMP_DIR/vdev2 $PEER_DIR/vdev2
log_must ln $PEER_DIR/vdev1 $MMP_DIR/vdev1

log_must mmp_clear_hostid
log_must mmp_set_hostid $HOSTID2
log_must eval "ZFS_HOSTID=$HOSTID1 zhack -d $PEER_DIR action idle -t120 \
    $MMP_POOL >$MMP_ZHACK_LOG 2>&1 &"

#
# Wait for the peer to hold the pool the same way mmp_pool_create_zhack does.
# Grepping its log for the activity check line would work only by accident:
# ZFS_HOSTID is parsed with strtoull(env, NULL, 0), so a leading zero makes it
# octal, and the peer's hostid happens not to match the label.  Write the
# hostid unambiguously and the check is skipped and the line never appears.
#
typeset -i tries=0
while ! is_pool_imported $MMP_POOL "-d $PEER_DIR"; do
	if (( tries++ > 30 )); then
		log_fail "peer never imported the pool"
	fi
	log_must pgrep -x zhack
	log_must sleep 4
done

log_mustnot zhack -d $MMP_DIR mmp reclaim $MMP_POOL
log_must pgrep -x zhack
log_must rm -f $MMP_ZHACK_LOG
mmp_pool_destroy $MMP_POOL $MMP_DIR
log_must rm -rf $PEER_DIR

# 3. Every top level vdev is still counted after a recovery
log_note "Verify both top level vdevs are counted after a recovery"
log_must mkdir -p $MMP_DIR
log_must rm -f $MMP_DIR/*
log_must truncate -s $MINVDEVSIZE $MMP_DIR/vdev1 $MMP_DIR/vdev2 \
    $MMP_DIR/vdev3 $MMP_DIR/vdev4
log_must mmp_clear_hostid
log_must mmp_set_hostid $HOSTID1
log_must zpool create -f -o cachefile=$MMP_CACHE $MMP_POOL \
    mirror $MMP_DIR/vdev1 $MMP_DIR/vdev2 mirror $MMP_DIR/vdev3 $MMP_DIR/vdev4
log_must zpool set multihost=on $MMP_POOL
crash_export_as_other_host
log_must mv $MMP_DIR/vdev4 $HIDDEN_LEG

log_mustnot zpool import -d $MMP_DIR -f $MMP_POOL
log_must zhack -d $MMP_DIR mmp reclaim $MMP_POOL

become_third_host
nums=$(claim_reimport)
if [[ "$nums" != "3 3" ]]; then
	log_fail "claim across two top levels counted '$nums', expected '3 3'"
fi
log_note "claim across two top levels required 3 writes and got 3"
mmp_pool_destroy $MMP_POOL $MMP_DIR
log_must rm -f $HIDDEN_LEG

# 4. A pool which does not use multihost is left alone
log_note "Verify a pool without multihost is left alone"
log_must mkdir -p $MMP_DIR
log_must rm -f $MMP_DIR/*
log_must truncate -s $MINVDEVSIZE $MMP_DIR/vdev1 $MMP_DIR/vdev2
log_must mmp_clear_hostid
log_must mmp_set_hostid $HOSTID1
log_must zpool create -f -o cachefile=$MMP_CACHE $MMP_POOL \
    mirror $MMP_DIR/vdev1 $MMP_DIR/vdev2
log_must zpool export -F $MMP_POOL
log_must mmp_clear_hostid
log_must mmp_set_hostid $HOSTID2
log_must mv $MMP_DIR/vdev2 $HIDDEN_LEG

log_mustnot zhack -d $MMP_DIR mmp reclaim $MMP_POOL
log_must zpool import -d $MMP_DIR -f $MMP_POOL
log_must check_state $MMP_POOL "" "DEGRADED"
if zpool status $MMP_POOL | grep -q OFFLINE; then
	log_fail "a leg was offlined on a pool which does not use multihost"
fi
mmp_pool_destroy $MMP_POOL $MMP_DIR
log_must rm -f $HIDDEN_LEG

# 5. Legs of a log vdev are not touched
log_note "Verify a log vdev leg is left alone"
log_must mkdir -p $MMP_DIR
log_must rm -f $MMP_DIR/*
log_must truncate -s $MINVDEVSIZE $MMP_DIR/vdev1 $MMP_DIR/vdev2 \
    $MMP_DIR/vdev3 $MMP_DIR/vdev4
log_must mmp_clear_hostid
log_must mmp_set_hostid $HOSTID1
log_must zpool create -f -o cachefile=$MMP_CACHE $MMP_POOL \
    mirror $MMP_DIR/vdev1 $MMP_DIR/vdev2 \
    log mirror $MMP_DIR/vdev3 $MMP_DIR/vdev4
log_must zpool set multihost=on $MMP_POOL
crash_export_as_other_host
log_must mv $MMP_DIR/vdev4 $HIDDEN_LEG

log_must zhack -d $MMP_DIR mmp reclaim $MMP_POOL

become_third_host
nums=$(claim_reimport)
if [[ "$nums" != "2 2" ]]; then
	log_fail "pool with an absent log leg counted '$nums', expected '2 2'"
fi
log_note "pool with an absent log leg still imports, claim required 2 got 2"
if zpool status $MMP_POOL | grep -q OFFLINE; then
	log_fail "a log vdev leg was offlined"
fi
mmp_pool_destroy $MMP_POOL $MMP_DIR
log_must rm -f $HIDDEN_LEG

# 6. Members of a raidz vdev are not touched
log_note "Verify a raidz member is left alone"
log_must mkdir -p $MMP_DIR
log_must rm -f $MMP_DIR/*
log_must truncate -s $MINVDEVSIZE $MMP_DIR/vdev1 $MMP_DIR/vdev2 \
    $MMP_DIR/vdev3 $MMP_DIR/vdev4
log_must mmp_clear_hostid
log_must mmp_set_hostid $HOSTID1
log_must zpool create -f -o cachefile=$MMP_CACHE $MMP_POOL \
    raidz2 $MMP_DIR/vdev1 $MMP_DIR/vdev2 $MMP_DIR/vdev3 $MMP_DIR/vdev4
log_must zpool set multihost=on $MMP_POOL
crash_export_as_other_host
log_must mv $MMP_DIR/vdev4 $HIDDEN_LEG

#
# A raidz vdev is required as parity+1 in aggregate rather than one write per
# member, so an absent member does not raise the requirement.  The claim holds
# while parity+1 members remain writeable, and offlining one would be a
# persistent change that buys nothing.
#
log_must eval "zhack -d $MMP_DIR mmp reclaim $MMP_POOL >$MMP_ZHACK_LOG 2>&1"
log_must grep -q "no absent leaves" $MMP_ZHACK_LOG
log_must rm -f $MMP_ZHACK_LOG

become_third_host
nums=$(claim_reimport)
if [[ "$nums" != "3 3" ]]; then
	log_fail "raidz2 with an absent member counted '$nums', expected '3 3'"
fi
log_note "raidz2 with an absent member imports, claim required 3 got 3"
if zpool status $MMP_POOL | grep -q OFFLINE; then
	log_fail "a raidz member was offlined"
fi

log_pass "zhack mmp reclaim recovers a stranded pool and relaxes no further"
