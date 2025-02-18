#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
#	Add a swap zvol, and consume most (not all) of the space. This test
#	used to fill up swap, which can hang the system.
#
# STRATEGY:
#	1. Create a new zvol and add it as swap
#	2. Fill //var/tmp with 80% the size of the zvol
#	5. Remove the new zvol, and restore original swap devices
#

verify_runnable "global"

function cleanup
{
	rm -rf $TEMPFILE

	if is_swap_inuse $swapdev ; then
		log_must swap_cleanup $swapdev
	fi
}

log_assert "Using a zvol as swap space, fill /var/tmp to 80%."

log_onexit cleanup

vol=$TESTPOOL/$TESTVOL
swapdev=${ZVOL_DEVDIR}/$vol
log_must swap_setup $swapdev

# Get 80% of the number of 512 blocks in the zvol
typeset -i count blks volsize=$(get_prop volsize $vol)
((blks = (volsize / 512) * 80 / 100))
# Use 'blks' to determine a count for dd based on a 1M block size.
((count = blks / 2048))

log_note "Fill 80% of swap"
log_must dd if=/dev/urandom of=$TEMPFILE bs=1048576 count=$count
log_must rm -f $TEMPFILE
log_must swap_cleanup $swapdev

log_pass "Using a zvol as swap space, fill /var/tmp to 80%."
