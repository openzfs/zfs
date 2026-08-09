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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Exceed the maximum limit for a reservation and ensure it fails.
#
# STRATEGY:
# 1. Create a reservation file system.
# 2. Set the reservation to an absurd value.
# 3. Verify the return code is an error.
#

verify_runnable "both"

RESERVATION="reserve"

function cleanup
{
	if datasetexists $TESTPOOL/$RESERVATION ; then
		log_must zfs unmount $TESTPOOL/$RESERVATION
		log_must zfs destroy $TESTPOOL/$RESERVATION
	fi
}

log_onexit cleanup

log_assert "Verify that a reservation > 2^64 -1 fails."

log_must zfs create $TESTPOOL/$RESERVATION

log_mustnot zfs set reservation=18446744073709551615 $TESTPOOL/$RESERVATION

log_pass "Unable to set a reservation > 2^64 - 1"
