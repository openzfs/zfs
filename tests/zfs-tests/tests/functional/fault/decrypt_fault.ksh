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
# Test that injected decryption errors are handled correctly.
#
# STRATEGY:
# 1. Create an encrypted dataset with a test file
# 2. Inject decryption errors on the file 20% of the time
# 3. Read the file to confirm that errors are handled correctly
# 4. Confirm that the decryption injection was added to the ZED logs
#

log_assert "Testing that injected decryption errors are handled correctly"

function cleanup
{
	log_must zinject -c all
	default_cleanup_noexit
}

log_onexit cleanup

default_mirror_setup_noexit $DISK1 $DISK2
log_must zfs set compression=off $TESTPOOL
log_must eval "echo 'password' | zfs create -o encryption=on \
	-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/fs"
mntpt=$(get_prop mountpoint $TESTPOOL/fs)
log_must mkfile 32M $mntpt/file1

log_must zinject -a -t data -e decrypt -f 20 $mntpt/file1
log_must zfs umount $TESTPOOL/fs
log_must zfs mount $TESTPOOL/fs

log_mustnot eval "cat $mntpt/file1 > /dev/null"
# Events are not supported on FreeBSD
if ! is_freebsd; then
	log_must eval "zpool events $TESTPOOL | grep -q 'authentication'"
fi

log_pass "Injected decryption errors are handled correctly"
