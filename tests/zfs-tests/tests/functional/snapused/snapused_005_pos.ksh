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
# Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/snapused/snapused.kshlib

#
# DESCRIPTION:
#	Verify usedbysnapshots is correct.
#
# STRATEGY:
#	1. Create a filesystem.
#	2. Make file in the filesystem.
#	3. Snapshot it.
#	4. Check check_usedbysnapshots is correct.
#

verify_runnable "both"

function cleanup
{
	datasetexists $USEDTEST && destroy_dataset $USEDTEST -rR
}

log_assert "Verify usedbysnapshots is correct."
log_onexit cleanup

log_must zfs create $USEDTEST
check_usedbysnapshots $USEDTEST

typeset -i i=0
typeset -i r_size=0
mntpnt=$(get_prop mountpoint $USEDTEST)
while ((i < 5)); do
	((r_size=(i+1)*16))

	log_must mkfile "$r_size"M $mntpnt/file$i

	log_must zfs snapshot $USEDTEST@snap$i
	check_usedbysnapshots $USEDTEST

        ((i = i + 1))
done

log_pass "Verify usedbysnapshots is correct."
