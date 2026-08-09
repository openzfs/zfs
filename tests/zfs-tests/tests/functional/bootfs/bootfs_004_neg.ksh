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
