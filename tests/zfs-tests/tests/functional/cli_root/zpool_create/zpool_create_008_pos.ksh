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
