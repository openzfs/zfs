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
# A reservation of 'none' (which is an alias for 0) should be allowed. This
# test verifies that is true.
#
# STRATEGY:
# 1. Create a new file system in the test pool.
# 2. Set the reservation to 'none'.
# 3. Verify the associated reservation is indeed 0.
# 4. Repeat with reservation set to 0.
#

verify_runnable "both"

# Use a unique value so earlier test failures will not impact this test.
RESERVATION="reserve"-$$
RESERVATION2="reserve2"-$$

function cleanup
{
	typeset FS
	for FS in $TESTPOOL/$RESERVATION $TESTPOOL/$RESERVATION2
	do
		if datasetexists $FS ; then
			log_must zfs unmount $FS
			log_must zfs destroy $FS
		fi
	done
}

log_onexit cleanup

log_assert "Ensure a reservation of 0 or 'none' is allowed."

log_must zfs create $TESTPOOL/$RESERVATION
log_must zfs create $TESTPOOL/$RESERVATION2

log_must zfs set reservation=0 $TESTPOOL/$RESERVATION
log_must zfs set reservation=none $TESTPOOL/$RESERVATION2

for FS in $TESTPOOL/$RESERVATION $TESTPOOL/$RESERVATION2
do
	log_must [ $(zfs get -pHo value reservation $FS) -eq 0    ]
	log_must [ $(zfs get  -Ho value reservation $FS) =   none ]
done

log_pass "Successfully set reservation to 0 and 'none'"
