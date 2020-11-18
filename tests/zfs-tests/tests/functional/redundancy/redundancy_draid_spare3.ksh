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
# Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/redundancy/redundancy.kshlib

#
# DESCRIPTION:
# Verify dRAID resilver to traditional and distributed spares for
# a variety of pool configurations and pool states.
#
# STRATEGY:
# 1. For resilvers:
#    a. Create a semi-random dRAID pool configuration which can
#       sustain 1 failure and has 5 distributed spares.
#    b. Fill the pool with data
#    c. Systematically fault and replace vdevs in the pools with
#       spares to test resilving in common pool states.
#    d. Scrub the pool to verify no data was lost
#    e. Verify the contents of files in the pool
#

log_assert "Verify dRAID resilver"

function cleanup_tunable
{
	log_must set_tunable32 REBUILD_SCRUB_ENABLED 1
	cleanup
}

log_onexit cleanup_tunable

if is_kmemleak; then
	log_unsupported "Test case runs slowly when kmemleak is enabled"
fi

#
# Disable scrubbing after a sequential resilver to verify the resilver
# alone is able to reconstruct the data without the help of a scrub.
#
log_must set_tunable32 REBUILD_SCRUB_ENABLED 0

for replace_mode in "healing" "sequential"; do

	if [[ "$replace_mode" = "sequential" ]]; then
		flags="-s"
	else
		flags=""
	fi

	parity=1
	spares=5
	data=$(random_int_between 1 4)
	children=10
	draid="draid${parity}:${data}d:${children}c:${spares}s"

	setup_test_env $TESTPOOL $draid $children

	#
	# Perform a variety of replacements to normal and distributed spares
	# for a variety of different vdev configurations to exercise different
	# resilver code paths. The final configuration is expected to be:
	#
	# NAME                                  STATE     READ WRITE CKSUM
	# testpool                              DEGRADED     0     0     0
	#   draid1:1d:10c:5s-0                  DEGRADED     0     0     0
	#     /var/tmp/basedir.28683/new_vdev0  ONLINE       0     0     0
	#     /var/tmp/basedir.28683/new_vdev1  ONLINE       0     0     0
	#     spare-2                           DEGRADED     0     0     0
	#       /var/tmp/basedir.28683/vdev2    FAULTED      0     0     0
	#       draid1-0-3                      ONLINE       0     0     0
	#     spare-3                           DEGRADED     0     0     0
	#       /var/tmp/basedir.28683/vdev3    FAULTED      0     0     0
	#       draid1-0-4                      ONLINE       0     0     0
	#     /var/tmp/basedir.28683/vdev4      ONLINE       0     0     0
	#     /var/tmp/basedir.28683/vdev5      ONLINE       0     0     0
	#     /var/tmp/basedir.28683/vdev6      ONLINE       0     0     0
	#     draid1-0-0                        ONLINE       0     0     0
	#     spare-8                           DEGRADED     0     0     0
	#       /var/tmp/basedir.28683/vdev8    FAULTED      0     0     0
	#       draid1-0-1                      ONLINE       0     0     0
	#     spare-9                           ONLINE       0     0     0
	#       /var/tmp/basedir.28683/vdev9    ONLINE       0     0     0
	#       draid1-0-2                      ONLINE       0     0     0
	# spares
	#   draid1-0-0                          INUSE     currently in use
	#   draid1-0-1                          INUSE     currently in use
	#   draid1-0-2                          INUSE     currently in use
	#   draid1-0-3                          INUSE     currently in use
	#   draid1-0-4                          INUSE     currently in use
	#

	# Distributed spare which replaces original online device
	log_must check_vdev_state $TESTPOOL $BASEDIR/vdev7 "ONLINE"
	log_must zpool replace -w $flags $TESTPOOL $BASEDIR/vdev7 draid1-0-0
	log_must zpool detach $TESTPOOL $BASEDIR/vdev7
	log_must check_vdev_state $TESTPOOL draid1-0-0 "ONLINE"
	log_must check_hotspare_state $TESTPOOL draid1-0-0 "INUSE"

	# Distributed spare in mirror with original device faulted
	log_must zpool offline -f $TESTPOOL $BASEDIR/vdev8
	log_must check_vdev_state $TESTPOOL $BASEDIR/vdev8 "FAULTED"
	log_must zpool replace -w $flags $TESTPOOL $BASEDIR/vdev8 draid1-0-1
	log_must check_vdev_state $TESTPOOL spare-8 "DEGRADED"
	log_must check_vdev_state $TESTPOOL draid1-0-1 "ONLINE"
	log_must check_hotspare_state $TESTPOOL draid1-0-1 "INUSE"

	# Distributed spare in mirror with original device still online
	log_must check_vdev_state $TESTPOOL $BASEDIR/vdev9 "ONLINE"
	log_must zpool replace -w $flags $TESTPOOL $BASEDIR/vdev9 draid1-0-2
	log_must check_vdev_state $TESTPOOL spare-9 "ONLINE"
	log_must check_vdev_state $TESTPOOL draid1-0-2 "ONLINE"
	log_must check_hotspare_state $TESTPOOL draid1-0-2 "INUSE"

	# Normal faulted device replacement
	new_vdev0="$BASEDIR/new_vdev0"
	log_must truncate -s $MINVDEVSIZE $new_vdev0
	log_must zpool offline -f $TESTPOOL $BASEDIR/vdev0
	log_must check_vdev_state $TESTPOOL $BASEDIR/vdev0 "FAULTED"
	log_must zpool replace -w $flags $TESTPOOL $BASEDIR/vdev0 $new_vdev0
	log_must check_vdev_state $TESTPOOL $new_vdev0 "ONLINE"

	# Distributed spare faulted device replacement
	log_must zpool offline -f $TESTPOOL $BASEDIR/vdev2
	log_must check_vdev_state $TESTPOOL $BASEDIR/vdev2 "FAULTED"
	log_must zpool replace -w $flags $TESTPOOL $BASEDIR/vdev2 draid1-0-3
	log_must check_vdev_state $TESTPOOL spare-2 "DEGRADED"
	log_must check_vdev_state $TESTPOOL draid1-0-3 "ONLINE"
	log_must check_hotspare_state $TESTPOOL draid1-0-3 "INUSE"

	# Normal online device replacement
	new_vdev1="$BASEDIR/new_vdev1"
	log_must truncate -s $MINVDEVSIZE $new_vdev1
	log_must check_vdev_state $TESTPOOL $BASEDIR/vdev1 "ONLINE"
	log_must zpool replace -w $flags $TESTPOOL $BASEDIR/vdev1 $new_vdev1
	log_must check_vdev_state $TESTPOOL $new_vdev1 "ONLINE"

	# Distributed spare online device replacement (then fault)
	log_must zpool replace -w $flags $TESTPOOL $BASEDIR/vdev3 draid1-0-4
	log_must check_vdev_state $TESTPOOL spare-3 "ONLINE"
	log_must check_vdev_state $TESTPOOL draid1-0-4 "ONLINE"
	log_must check_hotspare_state $TESTPOOL draid1-0-4 "INUSE"
	log_must zpool offline -f $TESTPOOL $BASEDIR/vdev3
	log_must check_vdev_state $TESTPOOL $BASEDIR/vdev3 "FAULTED"
	log_must check_vdev_state $TESTPOOL spare-3 "DEGRADED"

	resilver_cksum=$(cksum_pool $TESTPOOL)
	if [[ $resilver_cksum != 0 ]]; then
		log_must zpool status -v $TESTPOOL
		log_fail "$replace_mode resilver cksum errors: $resilver_cksum"
	fi

	if [[ "$replace_mode" = "healing" ]]; then
		log_must zpool scrub -w $TESTPOOL
	else
		if [[ $(get_tunable REBUILD_SCRUB_ENABLED) -eq 0 ]]; then
			log_must zpool scrub -w $TESTPOOL
		else
			log_must wait_scrubbed $TESTPOOL
		fi
	fi

	log_must is_pool_scrubbed $TESTPOOL

	scrub_cksum=$(cksum_pool $TESTPOOL)
	if [[ $scrub_cksum != 0 ]]; then
		log_must zpool status -v $TESTPOOL
		log_fail "scrub cksum errors: $scrub_cksum"
	fi

	log_must check_pool_status $TESTPOOL "scan" "repaired 0B"
	log_must check_pool_status $TESTPOOL "scan" "with 0 errors"

	log_must is_data_valid $TESTPOOL

	cleanup
done

log_pass "Verify resilver to dRAID distributed spares"
