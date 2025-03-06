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
. $STF_SUITE/tests/functional/inuse/inuse.cfg

#
# DESCRIPTION:
# newfs will not interfere with devices and spare devices that are in use
# by active pool.
#
# STRATEGY:
# 1. Create a regular|mirror|raidz|raidz2 pool with the given disk
# 2. Try to newfs against the disk, verify it fails as expect.
#

verify_runnable "global"

if ! is_physical_device $FS_DISK0; then
	log_unsupported "This directory cannot be run on raw files."
fi

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1

	#
	# Tidy up the disks we used.
	#
	cleanup_devices $vdisks $sdisks
}

function verify_assertion #disks
{
	typeset targets=$1

	for t in $targets; do
		if new_fs $t; then
			log_fail "newfs over active pool " \
				"unexpected return code of 0"
		fi
	done

	return 0
}

log_assert "Verify newfs over active pool fails."

log_onexit cleanup

set -A vdevs "" "mirror" "raidz" "raidz1" "raidz2"

typeset -i i=0

unset NOINUSE_CHECK
while (( i < ${#vdevs[*]} )); do
	typeset spare="spare $sdisks"

	# If this is for raidz2, use 3 disks for the pool.
	[[ ${vdevs[i]} = "raidz2" ]] && spare="$sdisks"
	create_pool $TESTPOOL1 ${vdevs[i]} $vdisks $spare
	verify_assertion "$rawtargets"
	destroy_pool $TESTPOOL1

	(( i = i + 1 ))
done

log_pass "Newfs over active pool fails."
