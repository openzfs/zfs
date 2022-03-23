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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#
# Invalid pool names are rejected by zpool set bootfs
#
# STRATEGY:
#	1. Try to set bootfs on some non-existent pools
#
#
#

verify_runnable "global"

set -A pools "pool//$$" "pool%d123" "mirror" "c0t0d0s0" "pool*23*" "*po!l" \
	"%s££%^"

function cleanup {
	if poolexists $POOL; then
		log_must zpool destroy $POOL
	fi
	rm $TESTDIR/bootfs_004.$$.dat
}


log_assert "Invalid pool names are rejected by zpool set bootfs"
log_onexit cleanup

# here, we build up a large string and add it to the list of pool names
# a word to the ksh-wary, ${#array[@]} gives you the
# total number of entries in an array, so array[${#array[@]}]
# will index the last entry+1, ksh arrays start at index 0.
COUNT=0
while [ $COUNT -le 1025 ]
do
        bigname="${bigname}o"
        COUNT=$(( $COUNT + 1 ))
done
pools[${#pools[@]}]="$bigname"



mkfile $MINVDEVSIZE $TESTDIR/bootfs_004.$$.dat

typeset -i i=0;

while [ $i -lt "${#pools[@]}" ]
do
	POOL=${pools[$i]}/$TESTFS
	log_mustnot zpool create $POOL $TESTDIR/bootfs_004.$$.dat
	log_mustnot zfs create $POOL/$TESTFS
	log_mustnot zpool set bootfs=$POOL/$TESTFS $POOL

	i=$(( $i + 1 ))
done

log_pass "Invalid pool names are rejected by zpool set bootfs"
