#!/bin/ksh -p

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
#       - sustain N failures (1-3), and
#       - has N distributed spares to replace all faulted vdevs
#    b. Fill the pool with data
#    c. Systematically fault a vdev, then replace it with a spare
#    d. Scrub the pool to verify no data was lost
#    e. Verify the contents of files in the pool
#

log_assert "Verify resilver to dRAID distributed spares"

log_onexit cleanup

for replace_mode in "healing" "sequential"; do

	if [[ "$replace_mode" = "sequential" ]]; then
		flags="-s"
	else
		flags=""
	fi

	parity=$(random_int_between 1 3)
	spares=$(random_int_between $parity 3)
	data=$(random_int_between 1 8)

	(( min_children = (data + parity + spares) ))
	children=$(random_int_between $min_children 16)

	draid="draid${parity}:${data}d:${children}c:${spares}s"

	setup_test_env $TESTPOOL $draid $children

	i=0
	while [[ $i -lt $spares ]]; do
		fault_vdev="$BASEDIR/vdev$i"
		spare_vdev="draid${parity}-0-${i}"

		log_must zpool offline -f $TESTPOOL $fault_vdev
		log_must check_vdev_state $TESTPOOL $fault_vdev "FAULTED"
		log_must zpool replace -w $flags $TESTPOOL \
		    $fault_vdev $spare_vdev
		log_must check_vdev_state spare-$i "DEGRADED"
		log_must check_vdev_state $spare_vdev "ONLINE"
		log_must check_hotspare_state $TESTPOOL $spare_vdev "INUSE"
		log_must zpool detach $TESTPOOL $fault_vdev

		resilver_cksum=$(cksum_pool $TESTPOOL)
		if [[ $resilver_cksum != 0 ]]; then
			log_must zpool status -v $TESTPOOL
			log_fail "$replace_mode resilver "
			    "cksum errors: $resilver_cksum"
		fi

		if [[ "$replace_mode" = "healing" ]]; then
			log_must zpool scrub $TESTPOOL
		fi

		log_must wait_scrubbed $TESTPOOL
		log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
		log_must check_pool_status $TESTPOOL "scan" "with 0 errors"

		scrub_cksum=$(cksum_pool $TESTPOOL)
		if [[ $scrub_cksum != 0 ]]; then
			log_must zpool status -v $TESTPOOL
			log_fail "scrub cksum errors: $scrub_cksum"
		fi

		(( i += 1 ))
	done

	log_must is_data_valid $TESTPOOL

	cleanup
done

log_pass "Verify resilver to dRAID distributed spares"
