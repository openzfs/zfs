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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
# 'zpool create' have to use '-f' scenarios
#
# STRATEGY:
# 1. Prepare the scenarios
# 2. Create pool without '-f' and verify it fails
# 3. Create pool with '-f' and verify it succeeds
#

verify_runnable "global"

function cleanup
{
	if [[ $exported_pool == true ]]; then
		if [[ $force_pool == true ]]; then
			log_must zpool create -f $TESTPOOL $DISK0
		else
			log_must zpool import $TESTPOOL
		fi
	fi

	if poolexists $TESTPOOL ; then
                destroy_pool $TESTPOOL
	fi

	if poolexists $TESTPOOL1 ; then
                destroy_pool $TESTPOOL1
	fi
}

log_assert "'zpool create' have to use '-f' scenarios"
log_onexit cleanup

typeset exported_pool=false
typeset force_pool=false

# overlapped slices as vdev need -f to create pool

# Make the disk is EFI labeled first via pool creation
create_pool $TESTPOOL $DISK0
destroy_pool $TESTPOOL

# exported device to be as spare vdev need -f to create pool

log_must zpool create -f $TESTPOOL $DISK0
destroy_pool $TESTPOOL
create_pool $TESTPOOL $DISK0 $DISK1
log_must zpool export $TESTPOOL
exported_pool=true
log_mustnot zpool create $TESTPOOL1 $DISK1 spare $DISK2
create_pool $TESTPOOL1 $DISK1 spare $DISK2
force_pool=true
destroy_pool $TESTPOOL1

log_pass "'zpool create' have to use '-f' scenarios"
