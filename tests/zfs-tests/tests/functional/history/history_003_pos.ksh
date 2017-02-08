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
# Copyright (c) 2013, 2015 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	zpool history will truncate on small pools, leaving pool creation intact
#
# STRATEGY:
#	1. Create a test pool on a file.
#	2. Loop 300 times to set and remove compression to test dataset.
#	3. Make sure 'zpool history' output is truncated
#	4. Verify that the initial pool creation is preserved.
#

verify_runnable "global"

function cleanup
{
	datasetexists $spool && log_must $ZPOOL destroy $spool
	[[ -f $VDEV0 ]] && log_must $RM -f $VDEV0
	[[ -f $TMPFILE ]] && log_must $RM -f $TMPFILE
}

log_assert "zpool history limitation test."
log_onexit cleanup

mntpnt=$(get_prop mountpoint $TESTPOOL)
(( $? != 0 )) && log_fail "get_prop mountpoint $TESTPOOL"

VDEV0=$mntpnt/vdev0
log_must $MKFILE $MINVDEVSIZE $VDEV0

spool=smallpool.$$; sfs=smallfs.$$
log_must $ZPOOL create $spool $VDEV0
log_must $ZFS create $spool/$sfs

typeset -i orig_count=$($ZPOOL history $spool | $WC -l)
typeset orig_md5=$($ZPOOL history $spool | $HEAD -2 | $MD5SUM | \
    $AWK '{print $1}')

typeset -i i=0
while ((i < 300)); do
	$ZFS set compression=off $spool/$sfs
	$ZFS set compression=on $spool/$sfs
	$ZFS set compression=off $spool/$sfs
	$ZFS set compression=on $spool/$sfs
	$ZFS set compression=off $spool/$sfs

	((i += 1))
done

TMPFILE=/tmp/spool.$$
$ZPOOL history $spool >$TMPFILE
typeset -i entry_count=$($WC -l $TMPFILE | $AWK '{print $1}')
typeset final_md5=$($HEAD -2 $TMPFILE | $MD5SUM | $AWK '{print $1}')

$GREP 'zpool create' $TMPFILE >/dev/null 2>&1 ||
    log_fail "'zpool create' was not found in pool history"

$GREP 'zfs create' $TMPFILE >/dev/null 2>&1 &&
    log_fail "'zfs create' was found in pool history"

$GREP 'zfs set compress' $TMPFILE >/dev/null 2>&1 ||
    log_fail "'zfs set compress' was found in pool history"

# Verify that the creation of the pool was preserved in the history.
if [[ $orig_md5 != $final_md5 ]]; then
	log_fail "zpool creation history was not preserved."
fi

log_pass "zpool history limitation test passed."
