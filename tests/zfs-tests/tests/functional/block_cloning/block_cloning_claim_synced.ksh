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
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	A clone record from a TXG that is already synced must not be claimed.
#
#	An indirect write error makes zil_lwb_commit() sync the record's TXG
#	and continue through the commit list.  A later clone record can then
#	remain in the log even though its TXG is on disk.  If its blocks were
#	freed in that TXG, claiming the record references free space and a
#	debug build panics with "segment already in tree".
#
# STRATEGY:
#	1. In one TXG, write a destination, clone over it, overwrite it, and
#	   truncate the source, leaving all cloned blocks free.
#	2. Inject one indirect ZIL write error and fsync the destination.  The
#	   fallback syncs the TXG, then writes its clone record to the ZIL.
#	3. Copy the vdev to model a crash and verify that the clone record's
#	   TXG is no newer than the copy's newest uberblock.
#	4. Import the copy, scrub it, and verify that it has no leaks.
#

verify_runnable "global"

tunable_exists ZIL_FAULT_IO ||
	log_unsupported "A debug build with ZIL fault injection is required"

export VDIR=$TEST_BASE_DIR/disk-bclone-cs

claim="Synced clone records are not claimed."

log_assert $claim

log_must save_tunable TXG_TIMEOUT

function cleanup
{
	set_tunable32 ZIL_FAULT_IO 0
	restore_tunable TXG_TIMEOUT
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $VDIR
}

log_onexit cleanup

log_must rm -rf $VDIR
log_must mkdir -p $VDIR
log_must truncate -s $MINVDEVSIZE $VDIR/a

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $VDIR/a
log_must zfs create -o compression=off -o recordsize=128k \
    -o logbias=throughput -o atime=off $TESTPOOL/$TESTFS

typeset dir=/$TESTPOOL/$TESTFS
log_must dd if=/dev/urandom of=$VDIR/srcdata bs=128k count=4
log_must dd if=/dev/urandom of=$VDIR/w1 bs=128k count=4
log_must dd if=/dev/urandom of=$VDIR/w2 bs=128k count=4
log_must dd if=$VDIR/srcdata of=$dir/src bs=128k
log_must dd if=/dev/zero of=$dir/dst bs=128k count=4

# Create the ZIL header before the transaction under test.
log_must dd if=/dev/zero of=$dir/zil bs=1 count=1 conv=fsync
log_must sync_pool $TESTPOOL

# Stop the timeout clock from syncing the test TXG on its own.
log_must set_tunable32 TXG_TIMEOUT 3600
log_must sync_pool $TESTPOOL

log_must dd if=$VDIR/w1 of=$dir/dst bs=128k count=4 conv=notrunc
log_must clonefile -f $dir/src $dir/dst 0 0 524288
log_must dd if=$VDIR/w2 of=$dir/dst bs=128k count=4 conv=notrunc
log_must truncate -s 0 $dir/src

log_must set_tunable32 ZIL_FAULT_IO 1
log_must python3 -c '
import os, sys
fd = os.open(sys.argv[1], os.O_WRONLY)
os.fsync(fd)' "$dir/dst"
[[ $(get_tunable ZIL_FAULT_IO) == 0 ]] ||
	log_fail "The injected indirect ZIL write error was not consumed"
log_must cmp $dir/dst $VDIR/w2

# Save the state that a crash immediately after the fsync would leave.
log_must cp $VDIR/a $VDIR/a.save
log_must cmp $VDIR/a $VDIR/a.save
log_must restore_tunable TXG_TIMEOUT
log_must zpool export $TESTPOOL
log_must cp $VDIR/a.save $VDIR/a

typeset uber_txg
uber_txg=$(zdb -lu $VDIR/a | awk \
    '/^[ \t]*txg =/ && $3 > txg { txg = $3 } END { print txg + 0 }') ||
	log_fail "Could not read the saved vdev's uberblocks"

typeset records clone_txg
records=$(zdb -e -p $VDIR -ivv $TESTPOOL/$TESTFS 2>&1) ||
	log_fail "Could not read the saved intent log"
echo "$records"
clone_txg=$(echo "$records" | awk \
    '$1 == "TX_CLONE_RANGE" && $4 == "txg" {
	    gsub(",", "", $5)
	    print $5
	    exit
	}')
[[ -n $clone_txg ]] || log_fail "The saved log has no TX_CLONE_RANGE"
(( clone_txg <= uber_txg )) || log_fail \
    "Clone record TXG $clone_txg is newer than uberblock TXG $uber_txg"
log_note "Clone record TXG $clone_txg; newest uberblock TXG $uber_txg"

log_must zpool import -d $VDIR $TESTPOOL
log_must cmp $dir/dst $VDIR/w2
log_must [ ! -s $dir/src ]
log_must zpool scrub $TESTPOOL
log_must zpool wait -t scrub $TESTPOOL
log_must zpool export $TESTPOOL
log_must zdb -e -p $VDIR -bcc $TESTPOOL

log_pass $claim
