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
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/cli_common.kshlib

#
# DESCRIPTION:
#	Verify 'zfs receive' fails with malformed parameters.
#
# STRATEGY:
#	1. Denfine malformed parameters array
#	2. Feed the malformed parameters to 'zfs receive'
#	3. Verify the command should be failed
#

verify_runnable "both"

function cleanup
{
	typeset snap
	typeset bkup

	for snap in $init_snap $inc_snap $init_topsnap $inc_topsnap ; do
		snapexists $snap && \
			log_must $ZFS destroy -Rf $snap
	done

	for bkup in $full_bkup $inc_bkup $full_topbkup $inc_topbkup; do
		[[ -e $bkup ]] && \
			log_must $RM -f $bkup
	done
}

log_assert "Verify that invalid parameters to 'zfs receive' are caught."
log_onexit cleanup

init_snap=$TESTPOOL/$TESTFS@initsnap
inc_snap=$TESTPOOL/$TESTFS@incsnap
full_bkup=/var/tmp/full_bkup.$$
inc_bkup=/var/tmp/inc_bkup.$$

init_topsnap=$TESTPOOL@initsnap
inc_topsnap=$TESTPOOL@incsnap
full_topbkup=/var/tmp/full_topbkup.$$
inc_topbkup=/var/tmp/inc_topbkup.$$

log_must $ZFS snapshot $init_topsnap
log_must eval "$ZFS send $init_topsnap > $full_topbkup"
log_must $TOUCH /$TESTPOOL/foo

log_must $ZFS snapshot $inc_topsnap
log_must eval "$ZFS send -i $init_topsnap $inc_topsnap > $inc_topbkup"
log_must $TOUCH /$TESTPOOL/bar

log_must $ZFS snapshot $init_snap
log_must eval "$ZFS send $init_snap > $full_bkup"
log_must $TOUCH /$TESTDIR/foo

log_must $ZFS snapshot $inc_snap
log_must eval "$ZFS send -i $init_snap $inc_snap > $inc_bkup"
log_must $TOUCH /$TESTDIR/bar

$SYNC

set -A badargs \
    "" "nonexistent-snap" "blah@blah" "-d" "-d nonexistent-dataset" \
    "$TESTPOOL/$TESTFS" "$TESTPOOL1" "$TESTPOOL/fs@" "$TESTPOOL/fs@@mysnap" \
    "$TESTPOOL/fs@@" "$TESTPOOL/fs/@mysnap" "$TESTPOOL/fs@/mysnap" \
    "$TESTPOOL/nonexistent-fs/nonexistent-fs" "-d $TESTPOOL/nonexistent-fs" \
    "-d $TESTPOOL/$TESTFS/nonexistent-fs"

if is_global_zone ; then
	typeset -i n=${#badargs[@]}
	badargs[$n]="-d $TESTPOOL"
fi

typeset -i i=0
while (( i < ${#badargs[*]} ))
do
	for bkup in $full_bkup $inc_bkup $full_topbkup $inc_topbkup ; do
		log_mustnot eval "$ZFS receive ${badargs[i]} < $bkup"
	done

	(( i = i + 1 ))
done

log_pass "Invalid parameters to 'zfs receive' are caught as expected."
