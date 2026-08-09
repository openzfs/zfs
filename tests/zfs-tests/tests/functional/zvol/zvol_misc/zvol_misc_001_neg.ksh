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
#	Verify that using a zvol as a dump device works.
#
# STRATEGY:
# 1. Create a ZFS volume
# 2. Use dumpadm add the volume as dump device
# 3. Verify the return code as expected.
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
	zfs set volsize=$volsize $TESTPOOL/$TESTVOL
}

log_assert "Verify that a ZFS volume can act as dump device."
log_onexit cleanup

voldev=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL
savedumpdev=$(get_dumpdevice)

safe_dumpadm $voldev

log_pass "Verify that a ZFS volume can act as dump device."
