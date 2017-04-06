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

	reserve=`zfs get -pH reservation $FS | awk '{print $3}'`
	if [[ $reserve -ne 0 ]]; then
		log_fail "ZFS get -p reservation did not return 0"
	fi

	reserve=`zfs get -H reservation $FS | awk '{print $3}'`
	if [[ $reserve != "none" ]]; then
		log_fail "ZFS get reservation did not return 'none'"
	fi
done

log_pass "Successfully set reservation to 0 and 'none'"
