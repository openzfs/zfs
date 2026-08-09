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
#	'zfs rollback -f' will force unmount any filesystems.
#
# STRATEGY:
#	1. Create pool & fs.
#	2. Create the snapshot of this file system.
#	3. Write the mountpoint directory of this file system.
#	4. Make sure 'zfs rollback -f' succeeds.
#

verify_runnable "both"

log_assert "'zfs rollback -f' will force unmount any filesystems."
log_onexit cleanup_env

# Create a snapshot of this file system: FSSNAP0
setup_snap_env 1

#
# Write file and make the mountpoint directory busy when try to unmount
# the file system that was mounted on it.
#
write_mountpoint_dir ${FSSNAP0%%@*}

log_must zfs rollback $FSSNAP0
log_must zfs rollback -f $FSSNAP0
log_must datasetexists $FSSNAP0

pkill ${DD##*/}

check_files $FSSNAP0

log_pass "'zfs rollback -f' force unmount any filesystem passed."
