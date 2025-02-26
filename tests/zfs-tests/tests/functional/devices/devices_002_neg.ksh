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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/devices/devices.cfg
. $STF_SUITE/tests/functional/devices/devices_common.kshlib

#
# DESCRIPTION:
# When set property devices=off on file system, device files cannot be used
# in this file system.
#
# STRATEGY:
# 1. Create pool and file system.
# 2. Set devices=off on this file system.
# 3. Separately create block device file and character file.
# 4. Separately read and write from those two device files.
# 5. Check the return value, and make sure it failed.
#

verify_runnable "global"

log_assert "Setting devices=off on file system, the devices files in this file"\
	"system can not be used."
log_onexit cleanup

log_must zfs set devices=off $TESTPOOL/$TESTFS

#
# Create block device file backed by a ZFS volume.
# Verify it cannot be opened, written, and read.
#
create_dev_file b $TESTDIR/$TESTFILE1 $ZVOL_DEVDIR/$TESTPOOL/$TESTVOL
log_mustnot dd if=/dev/urandom of=$TESTDIR/$TESTFILE1 count=1 bs=128k
log_mustnot dd if=$TESTDIR/$TESTFILE1 of=/dev/null count=1 bs=128k

# Create character device file backed by /dev/null
# Verify it cannot be opened and written.
create_dev_file c $TESTDIR/$TESTFILE2
log_mustnot dd if=/dev/urandom of=$TESTDIR/$TESTFILE2 count=1 bs=128k

log_pass "Setting devices=off on file system and testing it pass."
