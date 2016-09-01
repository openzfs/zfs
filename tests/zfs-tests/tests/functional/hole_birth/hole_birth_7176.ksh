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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/hole_birth/hole_birth.kshlib

#
# DESCRIPTION:
#	zfs send -R send replication stream up to the named snap.
#
# STRATEGY:
#	1. Back up all the data from POOL/FS
#	2. Verify all the datasets and data can be recovered in POOL2
#	3. Back up all the data from root filesystem POOL2
#	4. Verify all the data can be recovered, too
#

verify_runnable "both"

log_assert "hole_birth bug illumos #7176 test"
log_onexit cleanup_pool $POOL2

test_fs_setup_7176 $POOL $POOL2

sendfs=$POOL/sendfs
recvfs=$POOL2/recvfs

log_must eval "$ZFS send $sendfs@snap1 > $BACKDIR/pool-snap1"
log_must eval "$ZFS receive -F $recvfs < $BACKDIR/pool-snap1"

log_must eval "$ZFS send -i $sendfs@snap1 $sendfs@snap2 > $BACKDIR/pool-snap1-snap2"
log_must eval "$ZFS receive $recvfs < $BACKDIR/pool-snap1-snap2"

log_must cmp_md5s /$sendfs/file1 /$recvfs/file1

# Cleanup POOL2
log_must cleanup_pool $POOL2

log_pass "hole_birth bug illumos #7176 test"
