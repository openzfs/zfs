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
#	Destroyed pools are not listed unless with -D option is specified.
#
# STRATEGY:
#	1. Create test pool A.
#	2. Destroy pool A.
#	3. Verify only 'import -D' can list pool A.
#

verify_runnable "global"

function cleanup
{
	destroy_pool $TESTPOOL1

	log_must rm $VDEV0 $VDEV1
	log_must mkfile $FILE_SIZE $VDEV0 $VDEV1
}

log_assert "Destroyed pools are not listed unless with -D option is specified."
log_onexit cleanup

log_must zpool create $TESTPOOL1 $VDEV0 $VDEV1
log_must zpool destroy $TESTPOOL1

#
# 'pool:' is the keywords of 'zpool import -D' output.
#
log_mustnot eval "zpool import -d $DEVICE_DIR | grep pool:"
log_must eval "zpool import -d $DEVICE_DIR -D | grep pool:"

log_pass "Destroyed pool only can be listed with -D option."
