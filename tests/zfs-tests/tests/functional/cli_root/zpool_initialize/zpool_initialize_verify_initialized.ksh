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
# Copyright (c) 2016 by Delphix. All rights reserved.
#
. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_initialize/zpool_initialize.kshlib

#
# DESCRIPTION:
# After initializing, the disk is actually initialized.
#
# STRATEGY:
# 1. Create a one-disk pool.
# 2. Initialize the disk to completion.
# 3. Load all metaslabs that don't have a spacemap, and make sure the entire
#    metaslab has been filled with the initializing pattern (deadbeef).
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
log_must mkfile $MINVDEVSIZE "$SMALLFILE"
log_must zpool create $TESTPOOL "$SMALLFILE"
log_must zpool initialize $TESTPOOL

while [[ "$(initialize_progress $TESTPOOL $SMALLFILE)" -lt "100" ]]; do
        sleep 0.5
done

log_must zpool export $TESTPOOL

spacemaps=0
bs=512
while read -r sm; do
        typeset offset="$(echo $sm | cut -d ' ' -f1)"
        typeset size="$(echo $sm | cut -d ' ' -f2)"

	spacemaps=$((spacemaps + 1))
        offset=$(((4 * 1024 * 1024) + 16#$offset))
	out=$(dd if=$SMALLFILE skip=$(($offset / $bs)) \
	    count=$(($size / $bs)) bs=$bs 2>/dev/null | od -t x8 -Ad)
	echo "$out" | log_must egrep "$PATTERN|\*|$size"
done <<< "$(zdb -p $TESTDIR -Pme $TESTPOOL | egrep 'spacemap[ ]+0 ' | \
    awk '{print $4, $8}')"

if [[ $spacemaps -eq 0 ]];then
	log_fail "Did not find any empty space maps to check"
else
	log_pass "Initializing wrote appropriate amount to disk"
fi
