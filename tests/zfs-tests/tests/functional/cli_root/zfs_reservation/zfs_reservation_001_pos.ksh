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
