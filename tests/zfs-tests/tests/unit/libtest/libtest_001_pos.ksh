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
# Tests for pool scan status
#

. $STF_SUITE/include/libtest.shlib

oneTimeSetUp() {
	MOCK_ZPOOL_OUTPUT=$(mktemp $TEST_BASE_DIR/zpool.XXXXXX)
}

zpool()
{
	cat $MOCK_ZPOOL_OUTPUT
}

oneTimeTearDown() {
	rm -f $MOCK_ZPOOL_OUTPUT
}

test_check_pool_status_resilvering()
{
	cat >$MOCK_ZPOOL_OUTPUT <<EOM
  random text to ignore
  scan: resilver in progress since Thu Feb 20 20:04:25 2014
EOM
	assertTrue "pool resilvering" \
	    "check_pool_status mockpool scan 'resilver in progress since '"
	assertTrue "is pool resilvering" "is_pool_resilvering mockpool"
	assertFalse "is pool resilvered" "is_pool_resilvered mockpool"
	assertFalse "is pool rebuilding" "is_pool_rebuilding mockpool"
	assertFalse "is pool rebuilt" "is_pool_rebuilt mockpool"
	assertFalse "is pool scrubbing" "is_pool_scrubbing mockpool"
	assertFalse "is pool scrubbed" "is_pool_scrubbed mockpool"
	assertFalse "is pool scrub stopped" "is_pool_scrub_stopped mockpool"
	assertFalse "is pool scrub paused" "is_pool_scrub_paused mockpool"
}


test_check_pool_status_resilvered()
{
	cat >$MOCK_ZPOOL_OUTPUT <<EOM
  scan: resilvered 38.8G in 0h5m with 0 errors on Mon Dec  9 15:01:18 2013
EOM
	assertTrue "pool resilvered" \
	    "check_pool_status mockpool scan 'resilvered '"
	assertFalse "is pool resilvering" "is_pool_resilvering mockpool"
	assertTrue "is pool resilvered" "is_pool_resilvered mockpool"
	assertFalse "is pool rebuilding" "is_pool_rebuilding mockpool"
	assertFalse "is pool rebuilt" "is_pool_rebuilt mockpool"
	assertFalse "is pool scrubbing" "is_pool_scrubbing mockpool"
	assertFalse "is pool scrubbed" "is_pool_scrubbed mockpool"
	assertFalse "is pool scrub stopped" "is_pool_scrub_stopped mockpool"
	assertFalse "is pool scrub paused" "is_pool_scrub_paused mockpool"
}

test_check_pool_status_rebuilding()
{
	cat >$MOCK_ZPOOL_OUTPUT <<EOM
  scan: rebuild in progress since Thu May  3 14:04:21 2018
EOM
	assertTrue "pool rebuilding" \
	    "check_pool_status mockpool scan 'rebuild in progress since '"
	assertFalse "is pool resilvering" "is_pool_resilvering mockpool"
	assertFalse "is pool resilvered" "is_pool_resilvered mockpool"
	assertTrue "is pool rebuilding" "is_pool_rebuilding mockpool"
	assertFalse "is pool rebuilt" "is_pool_rebuilt mockpool"
	assertFalse "is pool scrubbing" "is_pool_scrubbing mockpool"
	assertFalse "is pool scrubbed" "is_pool_scrubbed mockpool"
	assertFalse "is pool scrub stopped" "is_pool_scrub_stopped mockpool"
	assertFalse "is pool scrub paused" "is_pool_scrub_paused mockpool"
}

test_check_pool_status_rebuilt()
{
	cat >$MOCK_ZPOOL_OUTPUT <<EOM
  scan: rebuilt 600K in 0 days 0:00:01 with 0 errors on Apr 25 11:54:40 2018
EOM
	assertTrue "pool rebuilt" \
	    "check_pool_status mockpool scan 'rebuilt '"
	assertFalse "is pool resilvering" "is_pool_resilvering mockpool"
	assertFalse "is pool resilvered" "is_pool_resilvered mockpool"
	assertFalse "is pool rebuilding" "is_pool_rebuilding mockpool"
	assertTrue "is pool rebuilt" "is_pool_rebuilt mockpool"
	assertFalse "is pool scrubbing" "is_pool_scrubbing mockpool"
	assertFalse "is pool scrubbed" "is_pool_scrubbed mockpool"
	assertFalse "is pool scrub stopped" "is_pool_scrub_stopped mockpool"
	assertFalse "is pool scrub paused" "is_pool_scrub_paused mockpool"
}

test_check_pool_status_scrubbing()
{
	cat >$MOCK_ZPOOL_OUTPUT <<EOM
    scan: scrub in progress since Tue Dec 24 14:07:20 2013
EOM
	assertTrue "scrub in progress" \
	    "check_pool_status mockpool scan 'scrub in progress since '"
	assertFalse "is pool resilvering" "is_pool_resilvering mockpool"
	assertFalse "is pool resilvered" "is_pool_resilvered mockpool"
	assertFalse "is pool rebuilding" "is_pool_rebuilding mockpool"
	assertFalse "is pool rebuilt" "is_pool_rebuilt mockpool"
	assertTrue "is pool scrubbing" "is_pool_scrubbing mockpool"
	assertFalse "is pool scrubbed" "is_pool_scrubbed mockpool"
	assertFalse "is pool scrub stopped" "is_pool_scrub_stopped mockpool"
	assertFalse "is pool scrub paused" "is_pool_scrub_paused mockpool"
}

test_check_pool_status_scrubbed()
{
	cat >$MOCK_ZPOOL_OUTPUT <<EOM
  scan: scrub repaired 0B in 0 days
EOM
	assertTrue "scrub repaired" \
	    "check_pool_status mockpool scan 'scrub repaired '"
	assertFalse "is pool resilvering" "is_pool_resilvering mockpool"
	assertFalse "is pool resilvered" "is_pool_resilvered mockpool"
	assertFalse "is pool rebuilding" "is_pool_rebuilding mockpool"
	assertFalse "is pool rebuilt" "is_pool_rebuilt mockpool"
	assertFalse "is pool scrubbing" "is_pool_scrubbing mockpool"
	assertTrue "is pool scrubbed" "is_pool_scrubbed mockpool"
	assertFalse "is pool scrub stopped" "is_pool_scrub_stopped mockpool"
	assertFalse "is pool scrub paused" "is_pool_scrub_paused mockpool"
}

test_check_pool_status_scrub_stopped()
{
	cat >$MOCK_ZPOOL_OUTPUT <<EOM
  scan: scrub canceled on Fri Oct 12 12:50:55 2018
EOM
	assertTrue "scrub canceled" \
	    "check_pool_status mockpool scan 'scrub canceled '"
	assertFalse "is pool resilvering" "is_pool_resilvering mockpool"
	assertFalse "is pool resilvered" "is_pool_resilvered mockpool"
	assertFalse "is pool rebuilding" "is_pool_rebuilding mockpool"
	assertFalse "is pool rebuilt" "is_pool_rebuilt mockpool"
	assertFalse "is pool scrubbing" "is_pool_scrubbing mockpool"
	assertFalse "is pool scrubbed" "is_pool_scrubbed mockpool"
	assertTrue "is pool scrub stopped" "is_pool_scrub_stopped mockpool"
	assertFalse "is pool scrub paused" "is_pool_scrub_paused mockpool"
}

test_check_pool_status_scrub_paused()
{
	cat >$MOCK_ZPOOL_OUTPUT <<EOM
  scan: scrub paused since Fri Oct 12 17:51:25 2018
EOM
	assertTrue "scrub paused" \
	    "check_pool_status mockpool scan 'scrub paused '"
	assertFalse "is pool resilvering" "is_pool_resilvering mockpool"
	assertFalse "is pool resilvered" "is_pool_resilvered mockpool"
	assertFalse "is pool rebuilding" "is_pool_rebuilding mockpool"
	assertFalse "is pool rebuilt" "is_pool_rebuilt mockpool"
	assertFalse "is pool scrubbing" "is_pool_scrubbing mockpool"
	assertFalse "is pool scrubbed" "is_pool_scrubbed mockpool"
	assertFalse "is pool scrub stopped" "is_pool_scrub_stopped mockpool"
	assertTrue "is pool scrub paused" "is_pool_scrub_paused mockpool"
}

. $STF_SUITE/include/shunit2
