#!/bin/ksh -p
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

#
# DESCRIPTION:
#	raidz refreservation=auto picks worst raidz vdev
#
# STRATEGY:
#	1. Create a pool with a single raidz vdev
#	2. For each block size [512b, 1k, 128k] or [4k, 8k, 128k]
#	    - create a volume
#	    - remember its refreservation
#	    - destroy the volume
#	3. Destroy the pool
#	4. Recreate the pool with one more disk in the vdev, then repeat steps
#	   2 and 3.
#
# NOTES:
#	1. This test will use up to 14 disks but can cover the key concepts with
#	   5 disks.
#	2. If the disks are a mixture of 4Kn and 512n/512e, failures are likely.
#

verify_runnable "global"

typeset -a alldisks=($DISKS)

# The larger the volsize, the better zvol_volsize_to_reservation() is at
# guessing the right number - though it is horrible with tiny blocks.  At 10M on
# ashift=12, the estimate may be over 26% too high.
volsize=100

function cleanup
{
	default_cleanup_noexit
	default_setup_noexit "${alldisks[0]}"
}

log_assert "raidz refreservation=auto picks worst raidz vdev"
log_onexit cleanup

poolexists "$TESTPOOL" && log_must_busy zpool destroy "$TESTPOOL"

# Testing tiny block sizes on ashift=12 pools causes so much size inflation
# that small test disks may fill before creating small volumes.  However,
# testing 512b and 1K blocks on ashift=9 pools is an ok approximation for
# testing the problems that arise from 4K and 8K blocks on ashift=12 pools.
if is_freebsd; then
	bps=$(diskinfo -v ${alldisks[0]} | awk '/sectorsize/ { print $1 }')
elif is_linux; then
	bps=$(lsblk -nrdo min-io /dev/${alldisks[0]})
fi
case "$bps" in
512)
	allshifts=(9 10 17)
	;;
4096)
	allshifts=(12 13 17)
	;;
*)
	log_fail "bytes/sector: $bps != (512|4096)"
	;;
esac
log_note "Testing in ashift=${allshifts[0]} mode"

typeset -A sizes=

#
# Determine the refreservation for a $volsize MiB volume on each raidz type at
# various block sizes.
#
for parity in 1 2 3; do
	raid=raidz$parity
	typeset -A sizes["$raid"]

	# Ensure we hit scenarios with and without skip blocks
	for ndisks in $((parity * 2)) $((parity * 2 + 1)); do
		typeset -a disks=(${alldisks[0..$((ndisks - 1))]})

		if (( ${#disks[@]} < ndisks )); then
			log_note "Too few disks to test $raid-$ndisks"
			continue
		fi

		typeset -A sizes["$raid"]["$ndisks"]

		log_must zpool create "$TESTPOOL" "$raid" "${disks[@]}"

		for bits in "${allshifts[@]}"; do
			vbs=$((1 << bits))
			log_note "Gathering refreservation for $raid-$ndisks" \
			    "volblocksize=$vbs"

			vol=$TESTPOOL/$TESTVOL
			log_must zfs create -V ${volsize}m \
			    -o volblocksize=$vbs "$vol"

			refres=$(zfs get -Hpo value refreservation "$vol")
			log_must test -n "$refres"
			sizes["$raid"]["$ndisks"]["$vbs"]=$refres

			log_must_busy zfs destroy "$vol"
		done

		log_must_busy zpool destroy "$TESTPOOL"
	done
done

# A little extra info is always helpful when diagnosing problems.  To
# pretty-print what you find in the log, do this in ksh:
#   typeset -A sizes=(...)
#   print -v sizes
log_note "sizes=$(print -C sizes)"

#
# Helper function for checking that refreservation is calculated properly in
# multi-vdev pools.  "Properly" is defined as assuming that all vdevs are as
# space inefficient as the worst one.
#
function check_vdevs {
	typeset raid=$1
	typeset nd1=$2
	typeset nd2=$3
	typeset -a disks1 disks2
	typeset vbs vol refres refres1 refres2 expect

	disks1=(${alldisks[0..$((nd1 - 1))]})
	disks2=(${alldisks[$nd1..$((nd1 + nd2 - 1))]})
	if (( ${#disks2[@]} < nd2 )); then
		log_note "Too few disks to test $raid-$nd1 + $raid=$nd2"
		return
	fi

	log_must zpool create -f "$TESTPOOL" \
	    "$raid" "${disks1[@]}" "$raid" "${disks2[@]}"

	for bits in "${allshifts[@]}"; do
		vbs=$((1 << bits))
		log_note "Verifying $raid-$nd1 $raid-$nd2 volblocksize=$vbs"

		vol=$TESTPOOL/$TESTVOL
		log_must zfs create -V ${volsize}m -o volblocksize=$vbs "$vol"
		refres=$(zfs get -Hpo value refreservation "$vol")
		log_must test -n "$refres"

		refres1=${sizes["$raid"]["$nd1"]["$vbs"]}
		refres2=${sizes["$raid"]["$nd2"]["$vbs"]}

		if (( refres1 > refres2 )); then
			log_note "Expecting refres ($refres) to match refres" \
			   "from $raid-$nd1 ($refres1)"
			log_must test "$refres" -eq "$refres1"
		else
			log_note "Expecting refres ($refres) to match refres" \
			   "from $raid-$nd1 ($refres2)"
			log_must test "$refres" -eq "$refres2"
		fi

		log_must zfs destroy "$vol"
	done

	log_must zpool destroy "$TESTPOOL"
}

#
# Verify that multi-vdev pools use the last optimistic size for all the
# permutations within a particular raidz variant.
#
for raid in "${!sizes[@]}"; do
	# ksh likes to create a [0] item for us.  Thanks, ksh!
	[[ $raid == "0" ]] && continue

	for nd1 in "${!sizes["$raid"][@]}"; do
		# And with an empty array we get one key, ''.  Thanks, ksh!
		[[ $nd1 == "0" || -z "$nd1" ]] && continue

		for nd2 in "${!sizes["$raid"][@]}"; do
			[[ $nd2 == "0" || -z "$nd2" ]] && continue

			check_vdevs "$raid" "$nd1" "$nd2"
		done
	done
done

log_pass "raidz refreservation=auto picks worst raidz vdev"
