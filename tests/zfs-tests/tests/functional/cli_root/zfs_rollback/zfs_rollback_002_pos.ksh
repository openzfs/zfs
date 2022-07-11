#!/bin/ksh -p
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
