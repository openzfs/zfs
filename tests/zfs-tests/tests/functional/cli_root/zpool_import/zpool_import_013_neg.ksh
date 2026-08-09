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
	poolexists $POOL_NAME && destroy_pool $POOL_NAME
	rm -f /$TESTPOOL/$POOL_FILE
}

log_assert "'zpool import' fails for pool that was not cleanly exported"
log_onexit cleanup

uncompress_pool
log_mustnot zpool import -d /$TESTPOOL $POOL_NAME
log_must zpool import -d /$TESTPOOL -f $POOL_NAME

log_pass "'zpool import' fails for pool that was not cleanly exported"
