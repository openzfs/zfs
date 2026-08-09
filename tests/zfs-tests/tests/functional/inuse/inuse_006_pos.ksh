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
# dumpadm will not interfere with devices and spare devices that are in use
# by active pool.
#
# STRATEGY:
# 1. Create a regular|mirror|raidz|raidz2 pool with the given disk
# 2. Try to dumpadm against the disk, verify it fails as expect.
#

verify_runnable "global"

function cleanup
{
	if [[ -n $PREVDUMPDEV ]]; then
		log_must dumpadm -u -d $PREVDUMPDEV
	fi

	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1

	#
	# Tidy up the disks we used.
	#
	cleanup_devices $vdisks $sdisks
}

function verify_assertion # disks
{
	typeset targets=$1

	for t in $targets; do
		log_mustnot dumpadm -d $t
	done

        return 0
}

log_assert "Verify dumpadm over active pool fails."

log_onexit cleanup

set -A vdevs "" "mirror" "raidz" "raidz1" "raidz2"

typeset -i i=0

PREVDUMPDEV=`dumpadm | awk '/Dump device/ {print $3}'`

unset NOINUSE_CHECK
while (( i < ${#vdevs[*]} )); do
	typeset spare="spare $sdisks"

	# If this is for raidz2, use 3 disks for the pool.
	[[ ${vdevs[i]} = "raidz2" ]] && spare="$sdisks"
	create_pool $TESTPOOL1 ${vdevs[i]} $vdisks $spare
	verify_assertion "$disktargets"
	destroy_pool $TESTPOOL1

	(( i = i + 1 ))
done

log_pass "Dumpadm over active pool fails."
