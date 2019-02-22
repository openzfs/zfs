#!/bin/ksh -p
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
# Copyright (c) 2018 by Datto Inc.
# All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fault/fault.cfg

#
# DESCRIPTION:
# Test that injected decompression errors are handled correctly.
#
# STRATEGY:
# 1. Create an compressed dataset with a test file
# 2. Inject decompression errors on the file 20% of the time
# 3. Read the file to confirm that errors are handled correctly
# 4. Confirm that the decompression injection was added to the ZED logs
#

log_assert "Testing that injected decompression errors are handled correctly"

function cleanup
{
	log_must set_tunable64 zfs_compressed_arc_enabled 1
	log_must zinject -c all
	default_cleanup_noexit
}

log_onexit cleanup

default_mirror_setup_noexit $DISK1 $DISK2
log_must set_tunable64 zfs_compressed_arc_enabled 0
log_must zfs create -o compression=on $TESTPOOL/fs
mntpt=$(get_prop mountpoint $TESTPOOL/fs)
write_compressible $mntpt 32m 1 0 "testfile"
log_must sync
log_must zfs umount $TESTPOOL/fs
log_must zfs mount $TESTPOOL/fs
log_must zinject -a -t data -e decompress -f 20 $mntpt/testfile.0
log_mustnot eval "cat $mntpt/testfile.0 > /dev/null"
log_must eval "zpool events $TESTPOOL | grep -q 'data'"

log_pass "Injected decompression errors are handled correctly"
