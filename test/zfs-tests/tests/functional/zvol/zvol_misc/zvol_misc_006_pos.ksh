#! /usr/bin/ksh -p
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
# ZFS volume as dump device, it should always have 128k volblocksize
#
# STRATEGY:
# 1. Create a ZFS volume
# 2. Use dumpadm set the volume as dump device
# 3. Verify the volume's volblocksize=128k
#

verify_runnable "global"

function cleanup
{
	typeset dumpdev=$(get_dumpdevice)
	if [[ $dumpdev != $savedumpdev ]] ; then
		safe_dumpadm $savedumpdev
	fi
}

log_assert "zfs volume as dumpdevice should have 128k volblocksize"
log_onexit cleanup

voldev=/dev/zvol/dsk/$TESTPOOL/$TESTVOL
savedumpdev=$(get_dumpdevice)

typeset oblksize=$($ZFS get -H -o value volblocksize $TESTPOOL/$TESTVOL)
log_note "original $TESTPOOL/$TESTVOL volblocksize=$oblksize"

safe_dumpadm $voldev

typeset blksize=$($ZFS get -H -o value volblocksize $TESTPOOL/$TESTVOL)

if [[ $blksize != "128K" ]]; then
	log_fail "ZFS volume $TESTPOOL/$TESTVOL volblocksize=$blksize"
fi

log_pass "zfs volume as dumpdevice should have 128k volblocksize"
