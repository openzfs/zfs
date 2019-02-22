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
# Copyright (c) 2018 by Datto. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_resilver/zpool_resilver.cfg

verify_runnable "global"
verify_disk_count "$DISKS" 3

default_mirror_setup_noexit $DISK1 $DISK2 $DISK3

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)

# Create 256M of data
log_must file_write -b 1048576 -c 256 -o create -d 0 -f $mntpnt/bigfile
log_pass
