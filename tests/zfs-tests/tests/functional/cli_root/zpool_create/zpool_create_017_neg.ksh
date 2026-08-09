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
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
#
# DESCRIPTION:
# 'zpool create' will fail with mountpoint exists and is not empty.
#
#
# STRATEGY:
# 1. Prepare the mountpoint put some stuff into it.
# 2. Verify 'zpool create' over that mountpoint fails.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $TESTDIR
}

log_assert "'zpool create' should fail with mountpoint exists and not empty."
log_onexit cleanup

if [[ ! -d $TESTDIR ]]; then
	log_must mkdir -p $TESTDIR
fi

typeset -i i=0

while (( i < 2 )); do
	log_must rm -rf $TESTDIR/*
	if (( i == 0 )); then
		log_must mkdir $TESTDIR/testdir
	else
		log_must touch $TESTDIR/testfile
	fi

	log_mustnot zpool create -m $TESTDIR -f $TESTPOOL $DISK0
	log_mustnot poolexists $TESTPOOL

	(( i = i + 1 ))
done

log_pass "'zpool create' fail as expected with mountpoint exists and not empty."
