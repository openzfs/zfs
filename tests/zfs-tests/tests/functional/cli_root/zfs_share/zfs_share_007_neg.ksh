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
    "-g" "-b" "-c" "-d" "--invalid" \
    "$TESTPOOL" "$TESTPOOL/$TESTFS" "$TESTPOOL\$TESTCTR\$TESTFS1"

log_assert "Verify that invalid share parameters and options are caught."
log_onexit cleanup

typeset -i i=0
while (( i < ${#badopts[*]} ))
do
	log_note "Setting sharenfs=${badopts[i]} $i "
	log_mustnot zfs set sharenfs="${badopts[i]}" $TESTPOOL/$TESTFS

	showshares_nfs | grep $option > /dev/null 2>&1
	if (( $? == 0 )); then
		log_fail "An invalid setting '$option' was propagated."
	fi

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
