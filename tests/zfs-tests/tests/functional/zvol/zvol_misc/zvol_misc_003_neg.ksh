#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
# Copyright 2016 Nexenta Systems, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
#	Verify creating a storage pool or running newfs on a zvol used as a
#	dump device is denied.
#
# STRATEGY:
# 1. Create a ZFS volume
# 2. Use dumpadm to set the volume as dump device
# 3. Verify creating a pool & running newfs on the zvol returns an error.
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

	if poolexists $TESTPOOL1 ; then
		destroy_pool $TESTPOOL1
	fi
	zfs set volsize=$volsize $TESTPOOL/$TESTVOL
}

log_assert "Verify zpool creation and newfs on dump zvol is denied."
log_onexit cleanup

voldev=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL
savedumpdev=$(get_dumpdevice)

safe_dumpadm $voldev

unset NOINUSE_CHECK
log_mustnot eval "new_fs $voldev > /dev/null 2>&1"

log_mustnot zpool create $TESTPOOL1 $voldev

log_pass "Verify zpool creation and newfs on dump zvol is denied."
