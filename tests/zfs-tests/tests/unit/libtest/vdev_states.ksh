#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2018, Richard Elling
#

#
# DESCRIPTION:
# Unit tests for the libtest.shlib shell functions
# Test for vdev and related functions
#
. $STF_SUITE/include/libtest.shlib

oneTimeSetUp()
{
	MOCK_ZPOOL_OUTPUT=$(mktemp $TEST_BASE_DIR/zpool.XXXXXX)
}

zpool()
{
	cat $MOCK_ZPOOL_OUTPUT
}

oneTimeTearDown()
{
	rm -f $MOCK_ZPOOL_OUTPUT
}

test_vdevs_in_pool()
{
	cat >$MOCK_ZPOOL_OUTPUT <<EOM
mockpool  1008M   880M   128M        -         -    46%    87%  1.00x  ONLINE  -
  mirror  1008M   880M   128M        -         -    46%    87%
    sdd1      -      -      -        -         -      -      -
    sde1      -      -      -        -         -      -      -
	disk1	1008M	876M	132M	-	-	45%	86%
EOM
	assertTrue "sdd1 in pool" "vdevs_in_pool mockpool sdd1"
	assertTrue "disk1 in pool" "vdevs_in_pool mockpool sdd1"
	assertTrue "sdd1 and disk1 in pool" "vdevs_in_pool mockpool sdd1 disk1"
	assertFalse "sdd not in pool" "vdevs_in_pool mockpool sdd"
	assertFalse "poolname is not vdev" "vdevs_in_pool mockpool tank"
}

test_check_vdev_state()  # also check_hotspare_state, check_slog_state
{
	cat >$MOCK_ZPOOL_OUTPUT <<EOM
  pool: mockpool
 state: ONLINE
  scan: none requested
config:

	NAME        STATE     READ WRITE CKSUM
	mockpool    ONLINE       0     0     0
	  mirror-0  ONLINE       0     0     0
	    sdd1    ONLINE       0     0     0
	    sde1    OFFLINE      0     0     0
	    disk1   REMOVED      0     0     0
	    ata     FAULTED      0     0     0
	  23        UNKNOWN      0     0     0
	logs
	    sdf     SPLIT        0     0     0
	spares
	    omaha   AVAIL        0     0     0
	    disk-with-a-very-long-device-name   INUSE     0     0     0

errors: No known data errors
EOM
	assertEquals "sdd1 online 2 args" ONLINE \
	    "$(get_device_state mockpool sdd1)"
	assertEquals "sdd1 online 3 args" ONLINE \
	    "$(get_device_state mockpool sdd1 mockpool)"
	assertEquals "sdd1 not a slog vdev" "" \
	    "$(get_device_state mockpool sdd1 logs)"
	assertEquals "sdd1 not a spares vdev" "" \
	    "$(get_device_state mockpool sdd1 spares)"

	assertEquals "sdf not a pool vdev 2 args" "" \
	    "$(get_device_state mockpool sdf)"
	assertEquals "sdf not a pool vdev 3 args" "" \
	    "$(get_device_state mockpool sdf mockpool)"

	assertEquals "sde1 offline" OFFLINE "$(get_device_state mockpool sde1)"
	assertEquals "disk1 removed" REMOVED \
	    "$(get_device_state mockpool disk1)"
	assertEquals "ata faulted" FAULTED "$(get_device_state mockpool ata)"
	assertEquals "23 unknown" UNKNOWN "$(get_device_state mockpool 23)"
	assertEquals "sdf split" SPLIT "$(get_device_state mockpool sdf logs)"
	assertEquals "omaha avail" AVAIL \
	    "$(get_device_state mockpool omaha spares)"
	assertEquals "disk-with-a-very-long-device-name inuse" INUSE \
	    "$(get_device_state mockpool disk-with-a-very-long-device-name \
	        spares)"

	assertEquals "no partial match" "" "$(get_device_state mockpool sde)"

	assertTrue "check_hotspare_state match" \
	    "check_hotspare_state mockpool omaha AVAIL"
	assertFalse "check_hotspare_state mismatch" \
	    "check_hotspare_state mockpool omaha INUSE"

	assertTrue "check_slog_state match" \
	    "check_slog_state mockpool sdf SPLIT"
	assertFalse "check_slog_state mismatch" \
	    "check_slog_state mockpool sdf ONLINE"
}

. $STF_SUITE/include/shunit2
