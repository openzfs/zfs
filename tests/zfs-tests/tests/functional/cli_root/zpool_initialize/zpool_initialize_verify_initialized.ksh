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

	rm -f $ZDBOUT
}
log_onexit cleanup

ZDBOUT="$TEST_BASE_DIR/zdbout.txt"

PATTERN="16045690984833335023" # 0xdeadbeefdeadbeef
SMALLFILE="$TESTDIR/smallfile"

ORIG_PATTERN=$(get_tunable INITIALIZE_VALUE)
log_must set_tunable64 INITIALIZE_VALUE $PATTERN

log_must mkdir "$TESTDIR"
log_must mkfile $MINVDEVSIZE "$SMALLFILE"
log_must zpool create $TESTPOOL "$SMALLFILE"
log_must zpool initialize $TESTPOOL
log_must zpool wait -t initialize $TESTPOOL
log_must zpool export $TESTPOOL

metaslabs=0
bs=512
zdb -p $TESTDIR -Pme $TESTPOOL >$ZDBOUT
log_note "zdb output: $(cat $ZDBOUT)"
awk '/spacemap  *0 / { print $4, $8 }' $ZDBOUT |
while read -r offset_size; do
	log_note "offset_size: '$offset_size'"

	typeset offset=$(echo $offset_size | cut -d ' ' -f1)
	typeset size=$(echo $offset_size | cut -d ' ' -f2)

	log_note "offset: '$offset'"
	log_note "size: '$size'"

	metaslabs=$((metaslabs + 1))
	offset=$(((4 * 1024 * 1024) + 16#$offset))
	log_note "decoded offset: '$offset'"
	dd if=$SMALLFILE skip=$((offset / bs)) count=$((size / bs)) bs=$bs |
	log_must egrep "$PATTERN|\*|$size"
done

if [[ $metaslabs -eq 0 ]]; then
	log_fail "Did not find any empty metaslabs to check"
else
	log_pass "Initializing wrote appropriate amount to disk"
fi
