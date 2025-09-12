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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2019 Joyent, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/refreserv/refreserv.cfg
. $STF_SUITE/tests/functional/zvol/zvol_misc/zvol_misc_common.kshlib

#
# DESCRIPTION:
#	raidz refreservation=auto accounts for extra parity and skip blocks
#
# STRATEGY:
#	1. Create a pool with a single raidz vdev
#	2. For each block size [4k, 8k, 128k]
#	    - create a volume
#	    - fully overwrite it
#	    - verify that referenced is less than or equal to reservation
#	    - destroy the volume
#	3. Destroy the pool
#	4. Recreate the pool with one more disk in the vdev, then repeat steps
#	   2 and 3.
#	5. Repeat all steps above for raidz2 and raidz3.
#
# NOTES:
#	1. This test will use up to 14 disks but can cover the key concepts with
#	   5 disks.
#	2. If the disks are a mixture of 4Kn and 512n/512e, failures are likely.
#	   Therefore, when creating the pool we specify 4Kn sectors.
#

verify_runnable "global"

typeset -a alldisks=($DISKS)

# The larger the volsize, the better zvol_volsize_to_reservation() is at
# guessing the right number.  At 10M on ashift=12, the estimate may be over 26%
# too high.
volsize=100

function cleanup
{
	default_cleanup_noexit
	default_setup_noexit "${alldisks[0]}"
}

log_assert "raidz refreservation=auto accounts for extra parity and skip blocks"
log_onexit cleanup

poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

ashift=12
allshifts=(12 13 17)
maxpct=110

log_note "Testing in ashift=${allshifts[0]} mode"

# This loop handles all iterations of steps 1 through 4 described in strategy
# comment above,
for parity in 1 2 3; do
	raid=raidz$parity

	# Ensure we hit scenarios with and without skip blocks
	for ndisks in $((parity * 2)) $((parity * 2 + 1)); do
		typeset -a disks=(${alldisks[0..$((ndisks - 1))]})

		if (( ${#disks[@]} < ndisks )); then
			log_note "Too few disks to test $raid-$ndisks"
			continue
		fi

		log_must zpool create -o ashift=$ashift "$TESTPOOL" "$raid" "${disks[@]}"

		for bits in "${allshifts[@]}"; do
			vbs=$((1 << bits))
			log_note "Testing $raid-$ndisks volblocksize=$vbs"

			vol=$TESTPOOL/$TESTVOL-$vbs
			zdev=$ZVOL_DEVDIR/$vol
			log_must zfs create -V ${volsize}m \
			    -o volblocksize=$vbs "$vol"
			block_device_wait $zdev
			blockdev_exists $zdev

			log_must timeout 120 dd if=/dev/urandom of=$zdev \
			    bs=1024k count=$volsize status=progress
			sync_pool $TESTPOOL

			ref=$(zfs get -Hpo value referenced "$vol")
			refres=$(zfs get -Hpo value refreservation "$vol")
			log_must test -n "$ref"
			log_must test -n "$refres"

			typeset -F2 deltapct=$((refres * 100.0 / ref))
			log_note "$raid-$ndisks refreservation $refres" \
			    "is $deltapct% of reservation $res"

			log_must test "$ref" -le "$refres"
			log_must test "$deltapct" -le $maxpct

			log_must_busy zfs destroy "$vol"
			blockdev_missing $zdev
		done

		log_must_busy zpool destroy "$TESTPOOL"
	done
done

log_pass "raidz refreservation=auto accounts for extra parity and skip blocks"
