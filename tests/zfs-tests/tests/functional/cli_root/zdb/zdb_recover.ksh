#!/bin/ksh

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2021 by Allan Jude.
#

. $STF_SUITE/include/libtest.shlib

#
# Description:
# zdb -r <dataset> <path> <destination>
# Will extract <path> (relative to <dataset>) to the file <destination>
# Similar to -R, except it does the work for you to find each record
#
# Strategy:
# 1. Create a pool
# 2. Write some data to a file
# 3. Extract the file
# 4. Compare the file to the original
#

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm $tmpfile
}

log_assert "Verify zdb -r <dataset> <path> <dest> extract the correct data."
log_onexit cleanup
init_data=$TESTDIR/file1
tmpfile="$TEST_BASE_DIR/zdb-recover"
write_count=8
blksize=131072
verify_runnable "global"
verify_disk_count "$DISKS" 2

default_mirror_setup_noexit $DISKS
file_write -o create -w -f $init_data -b $blksize -c $write_count
log_must zpool sync $TESTPOOL

output=$(zdb -r $TESTPOOL/$TESTFS file1 $tmpfile)
log_must cmp $init_data $tmpfile

log_pass "zdb -r <dataset> <path> <dest> extracts the correct data."
