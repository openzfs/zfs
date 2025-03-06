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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zpool history' can cope with simultaneous commands.
#
# STRATEGY:
#	1. Create test pool and test fs.
#	2. Loop 100 times, set properties to test fs simultaneously.
#	3. Wait for all the command execution complete.
#	4. Make sure all the commands was logged by 'zpool history'.
#

verify_runnable "global"

log_assert "'zpool history' can cope with simultaneous commands."

typeset -i orig_count=$(zpool history $spool | wc -l)

typeset -i i=0
while ((i < 10)); do
	zfs set compression=off $TESTPOOL/$TESTFS &
	zfs set atime=off $TESTPOOL/$TESTFS &
	zfs create $TESTPOOL/$TESTFS1 &
	zfs create $TESTPOOL/$TESTFS2 &
	zfs create $TESTPOOL/$TESTFS3 &

	wait

	zfs snapshot $TESTPOOL/$TESTFS1@snap &
	zfs snapshot $TESTPOOL/$TESTFS2@snap &
	zfs snapshot $TESTPOOL/$TESTFS3@snap &

	wait

	zfs clone $TESTPOOL/$TESTFS1@snap $TESTPOOL/clone1 &
	zfs clone $TESTPOOL/$TESTFS2@snap $TESTPOOL/clone2 &
	zfs clone $TESTPOOL/$TESTFS3@snap $TESTPOOL/clone3 &

	wait

	zfs promote $TESTPOOL/clone1 &
	zfs promote $TESTPOOL/clone2 &
	zfs promote $TESTPOOL/clone3 &

	wait

	zfs destroy $TESTPOOL/$TESTFS1 &
	zfs destroy $TESTPOOL/$TESTFS2 &
	zfs destroy $TESTPOOL/$TESTFS3 &

	wait

	zfs destroy -Rf $TESTPOOL/clone1 &
	zfs destroy -Rf $TESTPOOL/clone2 &
	zfs destroy -Rf $TESTPOOL/clone3 &

	wait
	((i += 1))
done

typeset -i entry_count=$(zpool history $spool | wc -l)

if ((entry_count - orig_count != 200)); then
	log_fail "The entries count error: entry_count=$entry_count " \
		 "orig_count = $orig_count"
fi

log_pass "'zpool history' can cope with simultaneous commands."
