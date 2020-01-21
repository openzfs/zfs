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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright 2019 Nexenta by DDN, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	When scrubbing, replace device should stop scrub and start resilvering.
#
# STRATEGY:
#	1. Setup filesys with data.
#	2. Slow down one disk
#	3. Start a scrub
#	4. Do a replace and verify the scrub stops and the resilver starts
#	5. After the resilver finishes, verify there's no checksum errors
#

verify_runnable "global"

log_assert "When scrubbing, replace device should stop scrub and start resilvering."

function cleanup
{
	log_must zinject -c all
	log_must set_tunable64 SCAN_VDEV_LIMIT $ZFS_SCAN_VDEV_LIMIT_DEFAULT
}

log_onexit cleanup

# Make the scrub slow
log_must zinject -d $DISK1 -D10:1 $TESTPOOL
log_must set_tunable64 SCAN_VDEV_LIMIT $ZFS_SCAN_VDEV_LIMIT_SLOW

log_must zpool scrub $TESTPOOL
log_must zpool replace $TESTPOOL $DISK2 $DISK3
log_must is_pool_resilvering $TESTPOOL

while ! is_pool_resilvered $TESTPOOL; do
	sleep 1
done

log_must check_state $TESTPOOL "$DISK3" "online"

log_pass "When scrubbing, replace device should stop scrub and start resilvering."
