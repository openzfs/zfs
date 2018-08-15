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
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg

#
# DESCRIPTION:
#	For pool may be in use from other system,
#	'zpool import' will prompt the warning and fails.
#
# STRATEGY:
#	1. Prepare rawfile that are created from other system.
#	2. Verify 'zpool import' will fail.
#	3. Verify 'zpool import -f' succeed.
#

verify_runnable "global"

POOL_NAME=unclean_export
POOL_FILE=unclean_export.dat

function uncompress_pool
{

	log_note "Creating pool from $POOL_FILE"
	log_must bzcat \
	    $STF_SUITE/tests/functional/cli_root/zpool_import/blockfiles/$POOL_FILE.bz2 \
	    > /$TESTPOOL/$POOL_FILE
	return 0
}

function cleanup
{
	poolexists $POOL_NAME && log_must zpool destroy $POOL_NAME
	[[ -e /$TESTPOOL/$POOL_FILE ]] && rm /$TESTPOOL/$POOL_FILE
	return 0
}

log_assert "'zpool import' fails for pool that was not cleanly exported"
log_onexit cleanup

uncompress_pool
log_mustnot zpool import -d /$TESTPOOL $POOL_NAME
log_must zpool import -d /$TESTPOOL -f $POOL_NAME

log_pass "'zpool import' fails for pool that was not cleanly exported"
