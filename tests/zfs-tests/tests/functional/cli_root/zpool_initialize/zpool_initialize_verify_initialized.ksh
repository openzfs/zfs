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
