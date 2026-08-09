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
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that invalid share parameters and options are caught.
#
# STRATEGY:
# 1. Create a ZFS file system.
# 2. For each option in the list, set the sharenfs property.
# 3. Verify that the error code and sharenfs property.
#

verify_runnable "both"

function cleanup {
	if is_global_zone; then
		log_must zfs set sharenfs=off $TESTPOOL/$TESTFS
	fi
}

set -A badopts \
    "r0" "r0=machine1" "r0=machine1:machine2" \
    "-g" "-b" "-c" "-d" "--invalid" "rw=[::1]a:[::2]" "rw=[::1" \
    "$TESTPOOL" "$TESTPOOL/$TESTFS" "$TESTPOOL\$TESTCTR\$TESTFS1"

log_assert "Verify that invalid share parameters and options are caught."
log_onexit cleanup

typeset -i i=0
while (( i < ${#badopts[*]} ))
do
	log_note "Setting sharenfs=${badopts[i]} $i "
	log_mustnot zfs set sharenfs="${badopts[i]}" $TESTPOOL/$TESTFS

	log_mustnot eval "showshares_nfs | grep -q ${badopts[i]}"

	#
	# To global zone, sharenfs must be set 'off' before malformed testing.
	# Otherwise, the malformed test return '0'.
	#
	# To non-global zone, sharenfs can be set even 'off' or 'on'.
	#
	if is_global_zone; then
		log_note "Resetting sharenfs option"
		log_must zfs set sharenfs=off $TESTPOOL/$TESTFS
	fi

	((i = i + 1))
done

log_pass "Invalid share parameters and options we caught as expected."
