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
#	Verify usedbydataset is correct.
#
# STRATEGY:
#	1. Create a filesystem.
#	2. Make file in the filesystem.
#	3. Snapshot it.
#	4. Clone it and make file in the cloned filesystem.
#	5. Check usedbydataset is correct.
#

verify_runnable "both"

function cleanup
{
	datasetexists $USEDTEST && destroy_dataset $USEDTEST -rR
}

log_assert "Verify usedbydataset is correct."
log_onexit cleanup

log_must zfs create $USEDTEST
check_usedbydataset $USEDTEST

typeset -i i=0
typeset -i r_size=0
mntpnt=$(get_prop mountpoint $USEDTEST)
while ((i < 5)); do
	((r_size=(i+1)*16))

	log_must mkfile 16M $mntpnt/file$i
	log_must mkfile "$r_size"M $mntpnt/file_var$i
	log_must zfs snapshot -r $USEDTEST@snap$i

	log_must zfs clone $USEDTEST@snap$i $USEDTEST/cln$i
	log_must zfs set is:cloned=yes $USEDTEST/cln$i

	mntpnt_cln=$(get_prop mountpoint $USEDTEST/cln$i)
	log_must mkfile 16M $mntpnt_cln/file_cln$i
	log_must mkfile "$r_size"M $mntpnt_cln/file_cln_var$i

	check_usedbydataset $USEDTEST

        ((i = i + 1))
done

log_pass "Verify usedbydataset is correct."
