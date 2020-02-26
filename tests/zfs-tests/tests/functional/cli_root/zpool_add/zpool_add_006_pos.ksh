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
