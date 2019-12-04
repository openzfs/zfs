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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/inuse/inuse.cfg

#
# DESCRIPTION:
# ZFS will not interfere with devices that are in use by dumpadm.
#
# STRATEGY:
# 1. Create crash dump device using 'dumpadm'
# 2. Try to create a ZFS pool using the 'dumpadm' crash dump device.
#

verify_runnable "global"

if is_linux; then
	log_unsupported "Test case isn't applicable to Linux"
fi

function cleanup
{
	#
	# Remove dump device.
	#
	if [[ -n $PREVDUMPDEV ]]; then
		log_must dumpadm -u -d $PREVDUMPDEV > /dev/null
	fi

	destroy_pool $TESTPOOL
}

log_assert "Ensure ZFS cannot use a device designated as a dump device"

log_onexit cleanup

typeset dumpdev=""
typeset diskslice=""

PREVDUMPDEV=`dumpadm | grep "Dump device" | awk '{print $3}'`

log_note "Zero $FS_DISK0 and place free space in to slice 0"
log_must cleanup_devices $FS_DISK0

diskslice="${DEV_DSKDIR}/${FS_DISK0}${SLICE0}"
log_note "Configuring $diskslice as dump device"
log_must dumpadm -d $diskslice > /dev/null

log_note "Confirm that dump device has been setup"
dumpdev=`dumpadm | grep "Dump device" | awk '{print $3}'`
[[ -z "$dumpdev" ]] && log_untested "No dump device has been configured"

[[ "$dumpdev" != "$diskslice" ]] && \
    log_untested "Dump device has not been configured to $diskslice"

log_note "Attempt to zpool the dump device"
unset NOINUSE_CHECK
log_mustnot zpool create $TESTPOOL "$diskslice"
log_mustnot poolexists $TESTPOOL

log_pass "Unable to zpool a device in use by dumpadm"
