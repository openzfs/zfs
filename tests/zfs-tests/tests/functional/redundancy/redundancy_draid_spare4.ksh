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
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2019, Datto Inc. All rights reserved.
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
# Copyright (c) 2026 by Seagate Technology, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/redundancy/redundancy.kshlib

#
# DESCRIPTION:
# Verify resilver to dRAID distributed spares.
#
# STRATEGY:
# 1. For resilvers:
#    a. Create a semi-random dRAID pool configuration which can:
#       - sustain N failures (1-3) * n, and
#       - has N * n distributed spares to replace all faulted vdevs
#       - n is the number of fail groups in the dRAID
#       - failures in the groups happen at the same time
#    b. Fill the pool with data
#    c. Systematically fault a vdev, then replace it with a spare
#    d. Scrub the pool to verify no data was lost
#    e. Verify the contents of files in the pool
#

log_assert "Verify resilver to dRAID distributed spares"

function cleanup_tunable
{
	log_must set_tunable32 REBUILD_SCRUB_ENABLED 1
	cleanup
}

log_onexit cleanup_tunable

log_must set_tunable32 REBUILD_SCRUB_ENABLED 0

for replace_mode in "healing" "sequential"; do

	if [[ "$replace_mode" = "sequential" ]]; then
		flags="-s"
	else
		flags=""
	fi

	parity=$(random_int_between 1 3)
	spares=$(random_int_between 1 $parity)
	data=$(random_int_between 1 8)

	(( min_children = (data + parity + spares) ))
	children=$(random_int_between $min_children 16)
	n=$(random_int_between 2 4)
	(( width = children * n ))
	off=$(random_int_between 0 $((children - parity - 1)))

	(( spares *= n ))

	draid="draid${parity}:${data}d:${children}c:${width}w:${spares}s"

	setup_test_env $TESTPOOL $draid $width

	for (( i=0; i < $spares; i+=$n )); do

		for (( j=$i; j < $((i+n)); j++ )); do
			fault_vdev="$BASEDIR/vdev$((i / n + (j % n) * children + off))"
			log_must zpool offline -f $TESTPOOL $fault_vdev
			log_must check_vdev_state $TESTPOOL $fault_vdev "FAULTED"
		done

		for (( j=$i; j < $((i+n)); j++ )); do
			fault_vdev="$BASEDIR/vdev$((i / n + (j % n) * children + off))"
			spare_vdev="draid${parity}-0-${j}"
			log_must zpool replace -w $flags $TESTPOOL \
			    $fault_vdev $spare_vdev
		done

		for (( j=$i; j < $((i+n)); j++ )); do
			fault_vdev="$BASEDIR/vdev$((i / n + (j % n) * children + off))"
			spare_vdev="draid${parity}-0-${j}"
			log_must check_vdev_state spare-$j "DEGRADED"
			log_must check_vdev_state $spare_vdev "ONLINE"
			log_must check_hotspare_state $TESTPOOL $spare_vdev "INUSE"
			log_must zpool detach $TESTPOOL $fault_vdev
		done

		log_must verify_pool $TESTPOOL
		log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
		log_must check_pool_status $TESTPOOL "scan" "with 0 errors"
	done

	# Fail remaining drives as long as parity permits.
	faults_left=$parity
	off=0
	for (( failed=$((spares/n)); failed < $parity; failed++ )); do
		# we can still fail disks
		(( ++off ))
		for (( i=0; i < $n; i++ )); do
			fault_vdev="$BASEDIR/vdev$((i * children + children - 1 - off))"
			log_must zpool offline -f $TESTPOOL $fault_vdev
			log_must check_vdev_state $TESTPOOL $fault_vdev "FAULTED"

			log_must verify_pool $TESTPOOL
			log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
			log_must check_pool_status $TESTPOOL "scan" "with 0 errors"
			(( faults_left > 0 && faults_left-- ))
		done
	done

	# Make sure that faults_left failures are still allowed, but no more.
	for (( i=0; i < $n; i++ )); do
		fault_vdev="$BASEDIR/vdev$((i * children + children - 1))"
		log_must zpool offline -f $TESTPOOL $fault_vdev
		if (( $i < $faults_left)); then
			log_must check_vdev_state $TESTPOOL $fault_vdev "FAULTED"
		else
			log_must check_vdev_state $TESTPOOL $fault_vdev "DEGRADED"
			break
		fi

		log_must verify_pool $TESTPOOL
		log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
		log_must check_pool_status $TESTPOOL "scan" "with 0 errors"
	done

	log_must is_data_valid $TESTPOOL
	log_must check_pool_status $TESTPOOL "errors" "No known data errors"

	cleanup
done

log_pass "Verify resilver to dRAID distributed spares"
