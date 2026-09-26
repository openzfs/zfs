#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2026 Dmitry R.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	The per-block RAIDZ reconstruction engine reads each block's parity
#	from the raidz_parity_epochs table by physical birth txg, so a single
#	raidz1 vdev can hold both parity-1 and parity-2 blocks. This test
#	verifies, on a disposable mixed-parity pool, that the recon engine
#	rebuilds BOTH parity classes through the scrub and resilver (replace)
#	paths and that the layout persists across export/import.
#
# STRATEGY:
#	1. Create a 5-wide raidz1 pool; write data (parity-1, born at create).
#	2. Offline-inject a {0:5:1, T:5:2} epoch table with zhack; reimport.
#	3. Write more data past the epoch boundary (parity-2); record cksums.
#	4. Scrub is clean -- recon reads both parity classes.
#	5. 'zpool replace' a member: the resilver completes with no errors --
#	   recon rebuilds the full column set of parity-2 blocks.
#	6. Data is byte-for-byte intact for both classes.
#	7. Export/import keeps the feature active and scrubs clean.
#

verify_runnable "global"

typeset TESTPOOL2=${TESTPOOL}_recon
typeset BASEDIR=$TEST_BASE_DIR/raidz_recon.$$
typeset -a VDEVS
typeset -i ndev=5
typeset SPARE=$BASEDIR/spare

function cleanup
{
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	rm -rf $BASEDIR
}

log_onexit cleanup
log_assert "per-block recon rebuilds mixed parity-1/parity-2 via scrub + resilver"

mkdir -p $BASEDIR
typeset i=0
while (( i < ndev )); do
	VDEVS[$i]=$BASEDIR/vdev$i
	log_must truncate -s 512M ${VDEVS[$i]}
	(( i = i + 1 ))
done
log_must truncate -s 512M $SPARE

# 5-wide raidz1. Data written now is parity-1 (born at pool creation).
log_must zpool create -f -o ashift=12 $TESTPOOL2 raidz1 ${VDEVS[@]}
log_must zfs create -o recordsize=1M $TESTPOOL2/data
log_must dd if=/dev/urandom of=/$TESTPOOL2/data/p1a bs=1M count=8
log_must dd if=/dev/urandom of=/$TESTPOOL2/data/p1b bs=1M count=8
log_must sync_pool $TESTPOOL2
log_must zpool export $TESTPOOL2

# Inject a parity-2 epoch boundary strictly above every existing block's
# physical birth. The label's pool-config txg can lag the newest uberblock
# (the final export txgs), so derive T from the best uberblock instead --
# T = max(ub_txg) + 1 is greater than any physical birth in the pool.
typeset -i TXG=$(zdb -lu ${VDEVS[0]} | awk '/txg = /{print $3}' | sort -n | tail -1)
typeset -i T=$((TXG + 1))
log_must zhack -d $BASEDIR raidz_epochs $TESTPOOL2 0 0:${ndev}:1 ${T}:${ndev}:2
log_must zpool import -d $BASEDIR $TESTPOOL2
log_must eval "zpool get -H -o value feature@raidz_parity_epochs $TESTPOOL2 | grep -q active"

# Data written now is parity-2 (physical birth past the epoch boundary).
log_must dd if=/dev/urandom of=/$TESTPOOL2/data/p2a bs=1M count=8
log_must dd if=/dev/urandom of=/$TESTPOOL2/data/p2b bs=1M count=8
log_must sync_pool $TESTPOOL2

typeset SUMS=$BASEDIR/sums
log_must eval "cksum /$TESTPOOL2/data/* > $SUMS"

# Recon reads both parity classes with no errors.
log_must zpool scrub -w $TESTPOOL2
log_must check_pool_status $TESTPOOL2 "errors" "No known data errors"

# Resilver path honours per-block parity: replace a member, expect 0 errors.
log_must zpool replace -w $TESTPOOL2 ${VDEVS[2]} $SPARE
log_must check_pool_status $TESTPOOL2 "errors" "No known data errors"
log_must eval "cksum /$TESTPOOL2/data/* > $BASEDIR/sums.resilver"
log_must diff $SUMS $BASEDIR/sums.resilver

# Layout + data persist across export/import; scrub stays clean.
log_must zpool export $TESTPOOL2
log_must zpool import -d $BASEDIR $TESTPOOL2
log_must eval "zpool get -H -o value feature@raidz_parity_epochs $TESTPOOL2 | grep -q active"
log_must zpool scrub -w $TESTPOOL2
log_must check_pool_status $TESTPOOL2 "errors" "No known data errors"
log_must eval "cksum /$TESTPOOL2/data/* > $BASEDIR/sums.reimport"
log_must diff $SUMS $BASEDIR/sums.reimport

log_pass "per-block recon rebuilt mixed parity-1/parity-2 across scrub, resilver, reimport"