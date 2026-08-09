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
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
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
	datasetexists $spool && log_must zpool destroy $spool
	[[ -f $VDEV0 ]] && log_must rm -f $VDEV0
	[[ -f $TMPFILE ]] && log_must rm -f $TMPFILE
}

log_assert "zpool history limitation test."
log_onexit cleanup

mntpnt=$(get_prop mountpoint $TESTPOOL)

VDEV0=$mntpnt/vdev0
log_must mkfile $MINVDEVSIZE $VDEV0

spool=smallpool.$$; sfs=smallfs.$$
log_must zpool create $spool $VDEV0
log_must zfs create $spool/$sfs

typeset -i orig_count=$(zpool history $spool | wc -l)
typeset orig_hash=$(zpool history $spool | head -2 | xxh128digest)
typeset -i i=0
while ((i < 300)); do
	zfs set compression=off $spool/$sfs
	zfs set compression=on $spool/$sfs
	zfs set compression=off $spool/$sfs
	zfs set compression=on $spool/$sfs
	zfs set compression=off $spool/$sfs

	((i += 1))
done

TMPFILE=$TEST_BASE_DIR/spool.$$
zpool history $spool >$TMPFILE
typeset -i entry_count=$(wc -l < $TMPFILE)
typeset final_hash=$(head -2 $TMPFILE | xxh128digest)

grep -q 'zpool create' $TMPFILE ||
    log_fail "'zpool create' was not found in pool history"

grep -q 'zfs create' $TMPFILE &&
    log_fail "'zfs create' was found in pool history"

grep -q 'zfs set compress' $TMPFILE ||
    log_fail "'zfs set compress' was found in pool history"

# Verify that the creation of the pool was preserved in the history.
if [[ $orig_hash != $final_hash ]]; then
	log_fail "zpool creation history was not preserved."
fi

log_pass "zpool history limitation test passed."
