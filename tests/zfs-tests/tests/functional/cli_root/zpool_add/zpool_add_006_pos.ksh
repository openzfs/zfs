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
# Copyright (c) 2014, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_add/zpool_add.kshlib

#
# DESCRIPTION:
# Adding a large number of file based vdevs to a zpool works.
#
# STRATEGY:
# 1. Create a file based pool.
# 2. Add 16 file based vdevs to it.
# 3. Attempt to add a file based vdev that's too small; verify failure.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -rf $TESTDIR
}

log_assert "Adding a large number of file based vdevs to a zpool works."
log_onexit cleanup

log_must mkdir -p $TESTDIR
log_must truncate -s $MINVDEVSIZE $TESTDIR/file.00
create_pool "$TESTPOOL1" "$TESTDIR/file.00"

vdevs_list=$(echo $TESTDIR/file.{01..16})
log_must truncate -s $MINVDEVSIZE $vdevs_list

log_must zpool add -f $TESTPOOL1 $vdevs_list
log_must vdevs_in_pool $TESTPOOL1 "$vdevs_list"

# Attempt to add a file based vdev that's too small.
log_must truncate -s 32m $TESTDIR/broken_file
log_mustnot zpool add -f $TESTPOOL1 ${TESTDIR}/broken_file
log_mustnot vdevs_in_pool $TESTPOOL1 ${TESTDIR}/broken_file

log_pass "Adding a large number of file based vdevs to a zpool works."
