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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_rollback/zfs_rollback_common.kshlib

#
# DESCRIPTION:
#	Separately verify 'zfs rollback ''|-f|-r|-rf|-R|-rR will fail in
#	different conditions.
#
# STRATEGY:
#	1. Create pool and file system
#	2. Create 'snap' and 'snap1' of this file system.
#	3. Run 'zfs rollback ""|-f <snap>' and it should fail.
#	4. Create 'clone1' based on 'snap1'.
#	5. Run 'zfs rollback -r|-rf <snap>' and it should fail.
#

verify_runnable "both"

function cleanup
{
	pkill ${DD##*/}
	for snap in $FSSNAP0 $FSSNAP1 $FSSNAP2; do
		snapexists $snap && destroy_dataset $snap -Rf
	done
}

log_assert "Separately verify 'zfs rollback ''|-f|-r|-rf will fail in " \
	"different conditions."
log_onexit cleanup

# Create snapshot1 and snapshot2 for this file system.
#
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP
create_snapshot $TESTPOOL/$TESTFS $TESTSNAP1

# Run 'zfs rollback ""|-f <snap>' and it should fail.
#
log_mustnot zfs rollback $TESTPOOL/$TESTFS@$TESTSNAP
log_mustnot zfs rollback -f $TESTPOOL/$TESTFS@$TESTSNAP

# Create 'clone1' based on 'snap1'.
#
create_clone $TESTPOOL/$TESTFS@$TESTSNAP1 $TESTPOOL/$TESTCLONE1

# Run 'zfs rollback -r|-rf <snap>' and it should fail.
#
log_mustnot zfs rollback -r $TESTPOOL/$TESTFS@$TESTSNAP
log_mustnot zfs rollback -rf $TESTPOOL/$TESTFS@$TESTSNAP

log_pass "zfs rollback ''|-f|-r|-rf will fail in different conditions " \
	"passed."
