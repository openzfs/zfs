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
#	A pool created with the blake3 feature enabled records the checksum
#	that dedup=on uses, as zfs_dedup_default_checksum selects it: blake3
#	by default, or sha256.  The pool keeps it when the tunable changes and
#	across export and import.  Any other pool uses sha256, including one on which the
#	feature was enabled later.  Explicit dedup checksums are unchanged.
#
# STRATEGY:
#	1. With the tunable at its default, create a pool with the default
#	   features.  Check that it records blake3, that dedup=on and
#	   dedup=verify write blake3 dedup blocks, also after setting the
#	   tunable to sha256 and exporting and importing the pool, and that
#	   dedup=sha256 writes sha256 blocks.
#	2. With the tunable set to sha256, create a pool with the default
#	   features.  Check that it records sha256, that dedup=on writes sha256
#	   blocks and leaves feature@blake3 enabled, also after setting the
#	   tunable to blake3 and exporting and importing the pool, and that
#	   dedup=blake3 writes blake3 blocks.
#	3. With the tunable at its default, create a pool with feature@blake3
#	   disabled and check that it records nothing and that dedup=on writes
#	   sha256 blocks.  Upgrade, export and import the pool, check that
#	   dedup=on still writes sha256 blocks and leaves the feature enabled,
#	   and that dedup=blake3 writes blake3 blocks.
#	4. With the tunable at its default, create a pool with a compatibility
#	   set that lacks blake3 and check that it records nothing and that
#	   dedup=on writes sha256 blocks.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

typeset -i nfiles=0

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_must restore_tunable DEDUP_DEFAULT_CHECKSUM
}

# Write a file to dataset $1 and require all four of its data blocks to be
# dedup blocks checksummed with $2.
function check_dedup_cksum
{
	typeset ds=$1
	typeset cksum=$2
	typeset file
	typeset -i n

	(( nfiles += 1 ))
	file=$(get_prop mountpoint $ds)/file$nfiles
	log_must dd if=/dev/urandom of=$file bs=128k count=4
	sync_pool $TESTPOOL
	# A trailing slash makes zdb read the root dataset, not the MOS.
	[[ $ds == */* ]] || ds=$ds/
	n=$(zdb -ddddddbbbbbb $ds "$(get_objnum $file)" | grep 'L0 DVA' |
	    grep -c " $cksum .* dedup ")
	(( n == 4 )) || log_fail "$ds: $n of 4 blocks are $cksum dedup blocks"
}

# Require the pool directory of $TESTPOOL to record checksum $1 (blake3 or
# sha256) for dedup=on, or nothing if $1 is empty.
function check_recorded
{
	typeset want
	typeset got

	case "$1" in
	blake3)	want=14 ;;
	sha256)	want=8 ;;
	*)	want="" ;;
	esac
	got=$(zdb -dddd $TESTPOOL 1 |
	    awk '$1 == "org.openzfs:dedup_checksum" { print $3 }')
	[[ "$got" == "$want" ]] || log_fail \
	    "$TESTPOOL records dedup checksum '$got', expected '$want'"
}

function check_feature
{
	typeset state

	state=$(get_pool_prop feature@blake3 $TESTPOOL)
	[[ "$state" == "$1" ]] ||
	    log_fail "feature@blake3 is $state on $TESTPOOL, expected $1"
}

log_assert "dedup=on uses the checksum recorded when the pool was created"
log_onexit cleanup
log_must save_tunable DEDUP_DEFAULT_CHECKSUM

# 1. The tunable at its default, blake3, on a pool with the blake3 feature.
log_must set_tunable32 DEDUP_DEFAULT_CHECKSUM 0
log_must zpool create -f -O dedup=on -O compression=off $TESTPOOL $DISKS
log_must zfs create -o dedup=verify $TESTPOOL/verify
log_must zfs create -o dedup=sha256 $TESTPOOL/sha256
check_recorded blake3
check_feature enabled
check_dedup_cksum $TESTPOOL blake3
check_dedup_cksum $TESTPOOL/verify blake3
check_dedup_cksum $TESTPOOL/sha256 sha256
check_feature active
log_must set_tunable32 DEDUP_DEFAULT_CHECKSUM 1
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
check_dedup_cksum $TESTPOOL blake3
check_dedup_cksum $TESTPOOL/verify blake3
log_must destroy_pool $TESTPOOL

# 2. The tunable set to sha256, on a pool with the blake3 feature.
log_must set_tunable32 DEDUP_DEFAULT_CHECKSUM 1
log_must zpool create -f -O dedup=on -O compression=off $TESTPOOL $DISKS
check_recorded sha256
check_dedup_cksum $TESTPOOL sha256
check_feature enabled
log_must set_tunable32 DEDUP_DEFAULT_CHECKSUM 0
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
check_dedup_cksum $TESTPOOL sha256
check_feature enabled
log_must zfs create -o dedup=blake3 $TESTPOOL/blake3
check_dedup_cksum $TESTPOOL/blake3 blake3
check_feature active
log_must destroy_pool $TESTPOOL

# 3. A pool on which the blake3 feature was enabled after creation, the
# tunable at its default.
log_must zpool create -f -o feature@blake3=disabled -O dedup=on \
    -O compression=off $TESTPOOL $DISKS
check_recorded ""
check_dedup_cksum $TESTPOOL sha256
log_must zpool upgrade $TESTPOOL
check_feature enabled
check_dedup_cksum $TESTPOOL sha256
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
check_recorded ""
check_dedup_cksum $TESTPOOL sha256
check_feature enabled
log_must zfs create -o dedup=blake3 $TESTPOOL/blake3
check_dedup_cksum $TESTPOOL/blake3 blake3
check_feature active
log_must destroy_pool $TESTPOOL

# 4. A pool whose compatibility set does not include blake3, the tunable
# at its default.
log_must zpool create -f -o compatibility=openzfs-2.1-linux -O dedup=on \
    -O compression=off $TESTPOOL $DISKS
check_feature disabled
check_recorded ""
check_dedup_cksum $TESTPOOL sha256
log_must destroy_pool $TESTPOOL

log_pass "dedup=on uses the checksum recorded when the pool was created"
