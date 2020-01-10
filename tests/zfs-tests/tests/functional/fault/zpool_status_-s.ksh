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
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
#	Verify zpool status -s (slow IOs) works
#
# STRATEGY:
#	1. Create a file
#	2. Inject slow IOs into the pool
#	3. Verify we can see the slow IOs with "zpool status -s".
#	4. Verify we can see delay events.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/zpool_script.shlib

DISK=${DISKS%% *}

verify_runnable "both"

default_mirror_setup_noexit $DISKS

function cleanup
{
	log_must zinject -c all
	log_must set_tunable64 ZIO_SLOW_IO_MS $OLD_SLOW_IO
	log_must set_tunable64 SLOW_IO_EVENTS_PER_SECOND $OLD_SLOW_IO_EVENTS
	default_cleanup_noexit
}

log_onexit cleanup

log_must zpool events -c

# Mark any IOs greater than 10ms as slow IOs
OLD_SLOW_IO=$(get_tunable ZIO_SLOW_IO_MS)
OLD_SLOW_IO_EVENTS=$(get_tunable SLOW_IO_EVENTS_PER_SECOND)
log_must set_tunable64 ZIO_SLOW_IO_MS 10
log_must set_tunable64 SLOW_IO_EVENTS_PER_SECOND 1000

# Create 20ms IOs
log_must zinject -d $DISK -D20:100 $TESTPOOL
log_must mkfile 1048576 /$TESTPOOL/testfile
log_must zpool sync $TESTPOOL

log_must zinject -c all
SLOW_IOS=$(zpool status -sp | grep "$DISK" | awk '{print $6}')
DELAY_EVENTS=$(zpool events | grep delay | wc -l)

if [ $SLOW_IOS -gt 0 ] && [ $DELAY_EVENTS -gt 0 ] ; then
	log_pass "Correctly saw $SLOW_IOS slow IOs and $DELAY_EVENTS delay events"
else
	log_fail "Only saw $SLOW_IOS slow IOs and $DELAY_EVENTS delay events"
fi
