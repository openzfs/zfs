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
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib
. $STF_SUITE/tests/functional/zvol/zvol_swap/zvol_swap.cfg

#
# DESCRIPTION:
# Verify that a zvol can be used as a swap device
#
# STRATEGY:
# 1. Create a pool
# 2. Create a zvol volume
# 3. Use zvol as swap space
# 4. Create a file under /var/tmp
#

verify_runnable "global"

function cleanup
{
	rm -rf $TEMPFILE

	if is_swap_inuse $voldev ; then
		log_must swap_cleanup $voldev
	fi
}

log_assert "Verify that a zvol can be used as a swap device"

log_onexit cleanup

voldev=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL
log_note "Add zvol volume as swap space"
log_must swap_setup $voldev

log_note "Create a file under /var/tmp"
log_must file_write -o create -f $TEMPFILE \
    -b $BLOCKSZ -c $NUM_WRITES -d $DATA

[[ ! -f $TEMPFILE ]] && log_fail "Unable to create file under /var/tmp"

filesize=`ls -l $TEMPFILE | awk '{print $5}'`
tf_size=$(( BLOCKSZ * NUM_WRITES ))
(( $tf_size != $filesize )) &&
    log_fail "testfile is ($filesize bytes), expected ($tf_size bytes)"

log_pass "Successfully added a zvol to swap area."
