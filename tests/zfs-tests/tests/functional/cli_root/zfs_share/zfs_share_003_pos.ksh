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
# Invoking "zfs share <file system>" with a file system
# whose sharenfs property is 'off' , will fail with a
# return code of 1 and issue an error message.
#
# STRATEGY:
# 1. Make sure that the ZFS file system is unshared.
# 2. Mount the file system using the various combinations
# - zfs set sharenfs=off <file system>
# - zfs set sharenfs=none <file system>
# 3. Verify that share failed with return code of 1.
#

verify_runnable "both"

set -A fs \
    "$TESTDIR" "$TESTPOOL/$TESTFS" \
    "$TESTDIR1" "$TESTPOOL/$TESTCTR/$TESTFS1"

function cleanup
{
	typeset -i i=0
	while (( i < ${#fs[*]} )); do
		log_must zfs inherit -r sharenfs ${fs[((i + 1))]}
		log_must unshare_fs ${fs[i]}

		((i = i + 2))
	done
}


#
# Main test routine.
#
# Given a mountpoint and file system this routine will attempt
# to share a legacy mountpoint and then verify the share fails as
# expected.
#
function test_legacy_share # mntp filesystem
{
	typeset mntp=$1
	typeset filesystem=$2

	not_shared $mntp || \
	    log_fail "File system $filesystem is already shared."

	if is_global_zone ; then
		log_must zfs set sharenfs=off $filesystem
		not_shared $mntp || \
		    log_fail "File system $filesystem is still shared (set sharenfs)."
	fi

	zfs share $filesystem
	ret=$?
	(( ret == 1)) || \
	    log_fail "'zfs share $filesystem' " \
		"unexpected return code of $ret."

	not_shared $mntp || \
	    log_fail "file system $filesystem is shared (zfs share)."
}

log_assert "Verify that 'zfs share' with a file system " \
        "whose sharenfs property is 'off'  " \
        "will fail with return code 1."
log_onexit cleanup

typeset -i i=0
while (( i < ${#fs[*]} )); do
	test_legacy_share ${fs[i]} ${fs[((i + 1))]}

	((i = i + 2))
done

log_pass "Verify that 'zfs share' with a file system " \
        "whose sharenfs property is 'off' fails."
