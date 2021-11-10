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
