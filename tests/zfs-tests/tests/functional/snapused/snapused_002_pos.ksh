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
. $STF_SUITE/tests/functional/snapused/snapused.kshlib

#
# DESCRIPTION:
#	Verify usedbychildren is correct.
#
# STRATEGY:
#	1. Create a filesystem.
#	2. Create sub filesystem and make file in it.
#	3. Set reservation of the sub filesystem.
#	4. Create volume under it.
#	5. Snapshot it.
#	6. Check usedbychildren is correct.
#

verify_runnable "both"

function cleanup
{
	datasetexists $USEDTEST && destroy_dataset $USEDTEST -rR
}

log_assert "Verify usedbychildren is correct."
log_onexit cleanup

log_must zfs create $USEDTEST
check_usedbychildren $USEDTEST

typeset -i i=0
typeset -i r_size=0
mntpnt=$(get_prop mountpoint $USEDTEST)
while ((i < 5)); do
	((r_size=(i+1)*16))

	log_must zfs create $USEDTEST/fs$i
	log_must zfs set reservation="$r_size"M $USEDTEST/fs$i
	log_must mkfile 48M $mntpnt/fs$i/file$i

	if is_global_zone; then
		log_must zfs create -V 32M $USEDTEST/vol$i
	fi

	log_must zfs snapshot -r $USEDTEST@snap$i

	check_usedbychildren $USEDTEST

        ((i = i + 1))
done

log_pass "Verify usedbychildren is correct."
