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
# Copyright (c) 2022 George Amanakis. All rights reserved.
#

#
# DESCRIPTION:
# Verify correct output with 'zpool status -v' after corrupting a file
#
# STRATEGY:
# 1. Create a pool, an encrypted filesystem and a file
# 2. zinject checksum errors
# 3. Unmount the filesystem and unload the key
# 4. Scrub the pool
# 5. Verify we report that errors were detected but we do not report
#	the filename since the key is not loaded.
# 6. Load the key and mount the encrypted fs.
# 7. Verify we report errors in the pool in 'zpool status -v'

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

DISK=${DISKS%% *}

function cleanup
{
	log_must zinject -c all
	destroy_pool $TESTPOOL2
	rm -f $TESTDIR/vdev_a
}

log_assert "Verify reporting errors with unloaded keys works"
log_onexit cleanup

typeset passphrase="password"
typeset file="/$TESTPOOL2/$TESTFS1/$TESTFILE0"

truncate -s $MINVDEVSIZE $TESTDIR/vdev_a
log_must zpool create -f -o feature@head_errlog=enabled $TESTPOOL2 $TESTDIR/vdev_a

log_must eval "echo $passphrase > /$TESTPOOL2/pwd"

log_must zfs create -o encryption=aes-256-ccm -o keyformat=passphrase \
    -o keylocation=file:///$TESTPOOL2/pwd -o primarycache=none \
    $TESTPOOL2/$TESTFS1

log_must dd if=/dev/urandom of=$file bs=1024 count=1024 oflag=sync
log_must eval "echo 'aaaaaaaa' >> "$file

corrupt_blocks_at_level $file 0
log_must zfs umount $TESTPOOL2/$TESTFS1
log_must zfs unload-key -a
log_must zpool sync $TESTPOOL2
log_must zpool scrub $TESTPOOL2
log_must zpool wait -t scrub $TESTPOOL2
log_must zpool status -v $TESTPOOL2
log_mustnot eval "zpool status -v $TESTPOOL2 | \
    grep \"permission denied\""
log_mustnot eval "zpool status -v $TESTPOOL2 | grep '$file'"

log_must eval "cat /$TESTPOOL2/pwd | zfs load-key $TESTPOOL2/$TESTFS1"
log_must zfs mount $TESTPOOL2/$TESTFS1
log_must zpool status -v $TESTPOOL2
log_must eval "zpool status -v $TESTPOOL2 | \
    grep \"Permanent errors have been detected\""
log_must eval "zpool status -v $TESTPOOL2 | grep '$file'"

log_pass "Verify reporting errors with unloaded keys works"
