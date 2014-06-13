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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cache/cache.cfg
. $STF_SUITE/tests/functional/cache/cache.kshlib

#
# DESCRIPTION:
#	Verify cache device must be a block device.
#
# STRATEGY:
#	1. Create a pool
#	2. Add different object as cache
#	3. Verify character devices and files fail
#

verify_runnable "global"

function cleanup_testenv
{
	cleanup
	if [[ -n $lofidev ]]; then
		log_must $LOFIADM -d $lofidev
	fi
}

log_assert "Cache device can only be block devices."
log_onexit cleanup_testenv

TESTVOL=testvol1$$
dsk1=${DISKS%% *}
log_must $ZPOOL create $TESTPOOL ${DISKS#$dsk1}

# Add nomal /dev/rdsk device
log_mustnot $ZPOOL add $TESTPOOL cache /dev/rdsk/${dsk1}s0
#log_must verify_cache_device $TESTPOOL $dsk1 'ONLINE'

# Add nomal file
log_mustnot $ZPOOL add $TESTPOOL cache $VDEV2

# Add /dev/rlofi device
lofidev=${VDEV2%% *}
log_must $LOFIADM -a $lofidev
lofidev=$($LOFIADM $lofidev)
log_mustnot $ZPOOL add $TESTPOOL cache "/dev/rlofi/${lofidev#/dev/lofi/}"
if [[ -n $lofidev ]]; then
	log_must $LOFIADM -d $lofidev
	lofidev=""
fi

# Add /dev/zvol/rdsk device
log_must $ZPOOL create $TESTPOOL2 $VDEV2
log_must $ZFS create -V $SIZE $TESTPOOL2/$TESTVOL
log_mustnot $ZPOOL add $TESTPOOL cache /dev/zvol/rdsk/$TESTPOOL2/$TESTVOL

log_pass "Cache device can only be block devices."
