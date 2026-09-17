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
# Verify an error log entry left behind by a destroyed snapshot does not
# hide the rest of the error log.
#
# STRATEGY:
# 1. Create a pool with a plain and an encrypted filesystem.
# 2. Snapshot the encrypted filesystem and unlink the file, so the block
#	only exists in the snapshot, then unload the key.
# 3. Inject an error in the snapshot and one in the plain filesystem, and
#	scrub.  With the key unloaded the snapshot entry is keyed by the
#	snapshot itself rather than by its head filesystem.
# 4. Load the key and destroy the snapshot, which leaves its entry in the
#	log keyed by a dataset that no longer exists.
# 5. Verify 'zpool status -v' still reports the error in the plain
#	filesystem, and reports the unresolvable one as a bare bookmark.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

function cleanup
{
	log_must zinject -c all
	destroy_pool $TESTPOOL2
	rm -f $TESTDIR/vdev_a
}

log_assert "A destroyed snapshot's error log entry does not hide the others"
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

log_must dd if=/dev/urandom of=$plainfile bs=1024 count=512 oflag=sync
log_must dd if=/dev/urandom of=$encfile bs=1024 count=512 oflag=sync

typeset -i plainobjset=$(get_prop objsetid $TESTPOOL2/plain)
typeset -i plainobj=$(get_objnum $plainfile)
typeset -i encobj=$(get_objnum $encfile)

log_must zfs snapshot $TESTPOOL2/$TESTFS1@snap1
typeset -i snapobjset=$(get_prop objsetid $TESTPOOL2/$TESTFS1@snap1)
log_must rm -f $encfile
log_must zpool sync $TESTPOOL2

log_must zfs umount $TESTPOOL2/$TESTFS1
log_must zfs unload-key $TESTPOOL2/$TESTFS1

# The snapshot entry is recorded while the key is unloaded, so it is keyed
# by the snapshot rather than by the head filesystem.
log_must zinject -b $(printf '%x:%x:0:0' $snapobjset $encobj) $TESTPOOL2
log_must zinject -b $(printf '%x:%x:0:0' $plainobjset $plainobj) $TESTPOOL2
log_must zpool scrub -w $TESTPOOL2
log_must zinject -c all

log_must zfs load-key $TESTPOOL2/$TESTFS1

log_must zfs destroy $TESTPOOL2/$TESTFS1@snap1
log_must zpool sync $TESTPOOL2

log_must zpool status -v $TESTPOOL2
log_mustnot eval "zpool status -v $TESTPOOL2 2>&1 | \
    grep 'List of errors unavailable'"
log_must eval "zpool status -v $TESTPOOL2 | \
    grep 'Permanent errors have been detected'"
log_must eval "zpool status -v $TESTPOOL2 | grep '$plainfile'"
# Neither the dataset nor the file name resolves any more.
log_must eval "zpool status -v $TESTPOOL2 | \
    grep -E '<0x[0-9a-f]+>:<0x[0-9a-f]+>'"

log_pass "A destroyed snapshot's error log entry does not hide the others"
