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
#	2. Fill //var/tmp (TEST_BASE_DIR) with 80% the size of the zvol
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

log_assert "Using a zvol as swap space, fill $TEST_BASE_DIR to 80%."

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

log_pass "Using a zvol as swap space, fill $TEST_BASE_DIR to 80%."
