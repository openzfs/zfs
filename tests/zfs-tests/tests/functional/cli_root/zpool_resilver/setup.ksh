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
