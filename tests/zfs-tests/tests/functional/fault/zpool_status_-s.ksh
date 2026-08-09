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
sync_pool $TESTPOOL

log_must zinject -c all
SLOW_IOS=$(zpool status -sp | awk -v d="$DISK" '$0 ~ d {print $6}')
DELAY_EVENTS=$(zpool events | grep -c delay)

log_must [ $SLOW_IOS -gt 0 ]
log_must [ $DELAY_EVENTS -gt 0 ]

log_pass "Correctly saw $SLOW_IOS slow IOs and $DELAY_EVENTS delay events"
