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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
# Copyright 2016 Nexenta Systems, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
# Verify the ability to take snapshots of zvols used as dump or swap.
#
# STRATEGY:
# 1. Create a ZFS volume
# 2. Set the volume as dump or swap
# 3. Verify creating a snapshot of the zvol succeeds.
#

verify_runnable "global"

if ! is_physical_device $DISKS; then
	log_unsupported "This directory cannot be run on raw files."
fi

volsize=$(zfs get -H -o value volsize $TESTPOOL/$TESTVOL)

function cleanup
{
	typeset dumpdev=$(get_dumpdevice)
	if [[ $dumpdev != $savedumpdev ]] ; then
		safe_dumpadm $savedumpdev
	fi

	swap -l | grep -qw $voldev && log_must swap -d $voldev

	typeset snap
	for snap in snap0 snap1 ; do
		datasetexists $TESTPOOL/$TESTVOL@$snap && \
			 destroy_dataset $TESTPOOL/$TESTVOL@$snap
	done
	zfs set volsize=$volsize $TESTPOOL/$TESTVOL
}

function verify_snapshot
{
	typeset volume=$1

	log_must zfs snapshot $volume@snap0
	log_must zfs snapshot $volume@snap1
	log_must datasetexists $volume@snap0 $volume@snap1

	log_must zfs destroy $volume@snap1
	log_must zfs snapshot $volume@snap1

	log_mustnot zfs rollback -r $volume@snap0
	log_must datasetexists $volume@snap0

	log_must zfs destroy -r $volume@snap0
}

log_assert "Verify the ability to take snapshots of zvols used as dump or swap."
log_onexit cleanup

voldev=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL
savedumpdev=$(get_dumpdevice)

# create snapshot over dump zvol
safe_dumpadm $voldev
log_must is_zvol_dumpified $TESTPOOL/$TESTVOL

verify_snapshot $TESTPOOL/$TESTVOL

safe_dumpadm $savedumpdev
log_mustnot is_zvol_dumpified $TESTPOOL/$TESTVOL

# create snapshot over swap zvol

log_must swap -a $voldev
log_mustnot is_zvol_dumpified $TESTPOOL/$TESTVOL

verify_snapshot $TESTPOOL/$TESTVOL

log_must swap -d $voldev
log_mustnot is_zvol_dumpified $TESTPOOL/$TESTVOL

log_pass "Creating snapshots from dump/swap zvols succeeds."
