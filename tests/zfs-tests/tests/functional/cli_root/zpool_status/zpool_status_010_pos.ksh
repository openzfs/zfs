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
# Copyright (c) 2026 by iXsystems, Inc.
#

#
# DESCRIPTION:
# Verify an error whose filesystem cannot be resolved does not hide the
# rest of the error log.
#
# STRATEGY:
# 1. Create a pool with a plain and an encrypted filesystem, and a file in
#	each.
# 2. Corrupt both files and scrub with the key loaded, so both errors are
#	recorded with a resolvable head filesystem.
# 3. Unmount the encrypted filesystem and unload its key.
# 4. Verify 'zpool status -v' still reports the error in the plain
#	filesystem, and the encrypted one without a filename.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

function cleanup
{
	destroy_pool $TESTPOOL2
	rm -f $TESTDIR/vdev_a
}

log_assert "An unresolvable error log entry does not hide the other errors"
log_onexit cleanup

typeset passphrase="password"
typeset plainfile="/$TESTPOOL2/plain/$TESTFILE0"
typeset encfile="/$TESTPOOL2/$TESTFS1/$TESTFILE0"

truncate -s $MINVDEVSIZE $TESTDIR/vdev_a
log_must zpool create -f -o feature@head_errlog=enabled $TESTPOOL2 \
    $TESTDIR/vdev_a

log_must zfs create -o primarycache=none $TESTPOOL2/plain
log_must eval "echo $passphrase > /$TESTPOOL2/pwd"
log_must zfs create -o encryption=aes-256-ccm -o keyformat=passphrase \
    -o keylocation=file:///$TESTPOOL2/pwd -o primarycache=none \
    $TESTPOOL2/$TESTFS1

log_must dd if=/dev/urandom of=$plainfile bs=1024 count=1024 oflag=sync
log_must dd if=/dev/urandom of=$encfile bs=1024 count=1024 oflag=sync

corrupt_blocks_at_level $plainfile 0
corrupt_blocks_at_level $encfile 0

# Scrub with the key loaded, so both entries name a resolvable filesystem.
log_must zpool scrub -w $TESTPOOL2
log_must eval "zpool status -v $TESTPOOL2 | grep '$plainfile'"
log_must eval "zpool status -v $TESTPOOL2 | grep '$encfile'"

log_must zfs umount $TESTPOOL2/$TESTFS1
log_must zfs unload-key $TESTPOOL2/$TESTFS1

log_must zpool status -v $TESTPOOL2
# The failure this guards against is printed on stderr, and no file list
# is printed at all, so both of these have to be checked.
log_mustnot eval "zpool status -v $TESTPOOL2 2>&1 | \
    grep 'List of errors unavailable'"
log_must eval "zpool status -v $TESTPOOL2 | \
    grep 'Permanent errors have been detected'"
log_must eval "zpool status -v $TESTPOOL2 | grep '$plainfile'"
# The head filesystem still resolves; only the file name does not.
log_must eval "zpool status -v $TESTPOOL2 | \
    grep '$TESTPOOL2/$TESTFS1:<0x'"

log_pass "An unresolvable error log entry does not hide the other errors"
