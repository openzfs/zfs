#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
