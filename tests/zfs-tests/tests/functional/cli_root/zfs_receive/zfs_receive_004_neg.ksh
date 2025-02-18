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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/cli_common.kshlib

#
# DESCRIPTION:
#	Verify 'zfs receive' fails with malformed parameters.
#
# STRATEGY:
#	1. Define malformed parameters array
#	2. Feed the malformed parameters to 'zfs receive'
#	3. Verify the command should be failed
#

verify_runnable "both"

function cleanup
{
	typeset snap
	typeset bkup

	for snap in $init_snap $inc_snap $init_topsnap $inc_topsnap ; do
		snapexists $snap && destroy_dataset $snap -Rf
	done

	for bkup in $full_bkup $inc_bkup $full_topbkup $inc_topbkup; do
		[[ -e $bkup ]] && \
			log_must rm -f $bkup
	done
}

log_assert "Verify that invalid parameters to 'zfs receive' are caught."
log_onexit cleanup

init_snap=$TESTPOOL/$TESTFS@initsnap
inc_snap=$TESTPOOL/$TESTFS@incsnap
full_bkup=$TEST_BASE_DIR/full_bkup.$$
inc_bkup=$TEST_BASE_DIR/inc_bkup.$$

init_topsnap=$TESTPOOL@initsnap
inc_topsnap=$TESTPOOL@incsnap
full_topbkup=$TEST_BASE_DIR/full_topbkup.$$
inc_topbkup=$TEST_BASE_DIR/inc_topbkup.$$

log_must zfs snapshot $init_topsnap
log_must eval "zfs send $init_topsnap > $full_topbkup"
log_must touch /$TESTPOOL/foo

log_must zfs snapshot $inc_topsnap
log_must eval "zfs send -i $init_topsnap $inc_topsnap > $inc_topbkup"
log_must touch /$TESTPOOL/bar

log_must zfs snapshot $init_snap
log_must eval "zfs send $init_snap > $full_bkup"
log_must touch /$TESTDIR/foo

log_must zfs snapshot $inc_snap
log_must eval "zfs send -i $init_snap $inc_snap > $inc_bkup"
log_must touch /$TESTDIR/bar

sync_all_pools

set -A badargs \
    "" "nonexistent-snap" "blah@blah" "-d" "-d nonexistent-dataset" \
    "$TESTPOOL1" "$TESTPOOL/fs@" "$TESTPOOL/fs@@mysnap" \
    "$TESTPOOL/fs@@" "$TESTPOOL/fs/@mysnap" "$TESTPOOL/fs@/mysnap" \
    "$TESTPOOL/nonexistent-fs/nonexistent-fs" "-d $TESTPOOL/nonexistent-fs" \
    "-d $TESTPOOL/$TESTFS/nonexistent-fs"

typeset -i i=0
while (( i < ${#badargs[*]} ))
do
	for bkup in $full_bkup $inc_bkup $full_topbkup $inc_topbkup ; do
		log_mustnot eval "zfs receive ${badargs[i]} < $bkup"
	done

	(( i = i + 1 ))
done

log_pass "Invalid parameters to 'zfs receive' are caught as expected."
