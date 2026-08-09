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
# Copyright (c) 2016 by Delphix. All rights reserved.
#
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# After initializing, the disk is actually initialized.
#
# STRATEGY:
# 1. Create a one-disk pool.
# 2. Initialize the disk to completion.
# 3. Load all metaslabs and make sure that each contains at least
#    once instance of the initializing pattern (deadbeef).
#

function cleanup
{
	set_tunable64 INITIALIZE_VALUE $ORIG_PATTERN
        zpool import -d $TESTDIR $TESTPOOL

        if datasetexists $TESTPOOL ; then
                zpool destroy -f $TESTPOOL
        fi
        if [[ -d "$TESTDIR" ]]; then
                rm -rf "$TESTDIR"
        fi
}
log_onexit cleanup

PATTERN="deadbeefdeadbeef"
SMALLFILE="$TESTDIR/smallfile"

ORIG_PATTERN=$(get_tunable INITIALIZE_VALUE)
log_must set_tunable64 INITIALIZE_VALUE $(printf %llu 0x$PATTERN)

log_must mkdir "$TESTDIR"
log_must truncate -s $MINVDEVSIZE "$SMALLFILE"
log_must zpool create $TESTPOOL "$SMALLFILE"
log_must zpool initialize -w $TESTPOOL
log_must zpool export $TESTPOOL

metaslabs=0
bs=512
zdb -p $TESTDIR -Pme $TESTPOOL | awk '/metaslab[ ]+[0-9]+/ { print $4, $8 }' |
while read -r offset size; do
	log_note "offset: '$offset'"
	log_note "size: '$size'"

	metaslabs=$((metaslabs + 1))
	offset=$(((4 * 1024 * 1024) + 16#$offset))
	log_note "vdev file offset: '$offset'"

	# Note we use '-t x4' instead of '-t x8' here because x8 is not
	# a supported format on FreeBSD.
	dd if=$SMALLFILE skip=$((offset / bs)) count=$((size / bs)) bs=$bs |
	    od -t x4 -Ad | grep -qE "deadbeef +deadbeef +deadbeef +deadbeef" ||
	    log_fail "Pattern not found in metaslab free space"
done

if [[ $metaslabs -eq 0 ]]; then
	log_fail "Did not find any metaslabs to check"
else
	log_pass "Initializing wrote to each metaslab"
fi
