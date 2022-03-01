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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

. $STF_SUITE/tests/functional/slog/slog.kshlib

#
# DESCRIPTION:
#	Verify slogs are replayed correctly.  This test is a direct
#	adaptation of the ziltest.sh script for the ZFS Test Suite.
#
#	The general idea is to build up an intent log from a bunch of
#	diverse user commands without actually committing them to the
#	file system.  Then copy the file system, replay the intent
#	log and compare the file system and the copy.
#
#	To enable this automated testing of the intent log some minimal
#	support is required of the file system.  In particular, a
#	"freeze" command is required to flush the in-flight transactions;
#	to stop the actual committing of transactions; and to ensure no
#	deltas are discarded. All deltas past a freeze point are kept
#	for replay and comparison later. Here is the flow:
#
# STRATEGY:
#	1. Create an empty file system (TESTFS)
#	2. Freeze TESTFS
#	3. Run various user commands that create files, directories and ACLs
#	4. Copy TESTFS to temporary location (TESTDIR/copy)
#	5. Unmount filesystem
#	   <at this stage TESTFS is empty again and unfrozen, and the
#	   intent log contains a complete set of deltas to replay it>
#	6. Remount TESTFS <which replays the intent log>
#	7. Compare TESTFS against the TESTDIR/copy
#

verify_runnable "global"

log_assert "Replay of intent log succeeds."
log_onexit cleanup
log_must setup

#
# 1. Create an empty file system (TESTFS)
#
log_must zpool create $TESTPOOL $VDEV log mirror $LDEV
log_must zfs set compression=on $TESTPOOL
log_must zfs create $TESTPOOL/$TESTFS

#
# This dd command works around an issue where ZIL records aren't created
# after freezing the pool unless a ZIL header already exists. Create a file
# synchronously to force ZFS to write one out.
#
log_must dd if=/dev/zero of=/$TESTPOOL/$TESTFS/sync \
    conv=fdatasync,fsync bs=1 count=1

#
# 2. Freeze TESTFS
#
log_must zpool freeze $TESTPOOL

#
# 3. Run various user commands that create files, directories and ACLs
#

# TX_CREATE
log_must touch /$TESTPOOL/$TESTFS/a

# TX_RENAME
log_must mv /$TESTPOOL/$TESTFS/a /$TESTPOOL/$TESTFS/b

# TX_SYMLINK
log_must touch /$TESTPOOL/$TESTFS/c
log_must ln -s /$TESTPOOL/$TESTFS/c /$TESTPOOL/$TESTFS/d

# TX_LINK
log_must touch /$TESTPOOL/$TESTFS/e
log_must ln /$TESTPOOL/$TESTFS/e /$TESTPOOL/$TESTFS/f

# TX_MKDIR
log_must mkdir /$TESTPOOL/$TESTFS/dir_to_delete

# TX_RMDIR
log_must rmdir /$TESTPOOL/$TESTFS/dir_to_delete

# Create a simple validation payload
log_must mkdir -p $TESTDIR
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/payload \
    oflag=sync bs=1k count=8
typeset checksum=$(sha256digest /$TESTPOOL/$TESTFS/payload)

# TX_WRITE (small file with ordering)
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/small_file \
    oflag=sync bs=1k count=1
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/small_file \
    oflag=sync bs=512 count=1

# TX_CREATE, TX_MKDIR, TX_REMOVE, TX_RMDIR
log_must cp -R /usr/share/dict /$TESTPOOL/$TESTFS
log_must rm -rf /$TESTPOOL/$TESTFS/dict

# TX_SETATTR
log_must touch /$TESTPOOL/$TESTFS/setattr
log_must chmod 567 /$TESTPOOL/$TESTFS/setattr
if is_freebsd; then
	log_must chgrp wheel /$TESTPOOL/$TESTFS/setattr
else
	log_must chgrp root /$TESTPOOL/$TESTFS/setattr
fi
log_must touch -cm -t 201311271200 /$TESTPOOL/$TESTFS/setattr

# TX_TRUNCATE (to zero)
log_must mkfile 4k /$TESTPOOL/$TESTFS/truncated_file
log_must truncate -s 0 /$TESTPOOL/$TESTFS/truncated_file

# TX_WRITE (large file)
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/large \
    oflag=sync bs=128k count=64

# Write zeros, which compress to holes, in the middle of a file
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/holes.1 \
    oflag=sync bs=128k count=8
log_must dd if=/dev/zero of=/$TESTPOOL/$TESTFS/holes.1 \
    oflag=sync bs=128k count=2

log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/holes.2 \
    oflag=sync bs=128k count=8
log_must dd if=/dev/zero of=/$TESTPOOL/$TESTFS/holes.2 \
    oflag=sync bs=128k count=2 seek=2

log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/holes.3 \
    oflag=sync bs=128k count=8
log_must dd if=/dev/zero of=/$TESTPOOL/$TESTFS/holes.3 \
    oflag=sync bs=128k count=2 seek=2 conv=notrunc

# TX_MKXATTR
log_must mkdir /$TESTPOOL/$TESTFS/xattr.dir
log_must touch /$TESTPOOL/$TESTFS/xattr.file
log_must set_xattr fileattr HelloWorld /$TESTPOOL/$TESTFS/xattr.dir
log_must set_xattr tmpattr HelloWorld /$TESTPOOL/$TESTFS/xattr.dir
log_must rm_xattr fileattr /$TESTPOOL/$TESTFS/xattr.dir

log_must set_xattr fileattr HelloWorld /$TESTPOOL/$TESTFS/xattr.file
log_must set_xattr tmpattr HelloWorld /$TESTPOOL/$TESTFS/xattr.file
log_must rm_xattr tmpattr /$TESTPOOL/$TESTFS/xattr.file

# TX_WRITE, TX_LINK, TX_REMOVE
# Make sure TX_REMOVE won't affect TX_WRITE if file is not destroyed
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS/link_and_unlink \
    oflag=sync bs=128k count=8
log_must ln /$TESTPOOL/$TESTFS/link_and_unlink \
   /$TESTPOOL/$TESTFS/link_and_unlink.link
log_must rm /$TESTPOOL/$TESTFS/link_and_unlink.link

#
# 4. Copy TESTFS to temporary location (TESTDIR/copy)
#
log_must mkdir -p $TESTDIR
log_must rsync -aHAX /$TESTPOOL/$TESTFS/ $TESTDIR/copy

#
# 5. Unmount filesystem and export the pool
#
# At this stage TESTFS is empty again and frozen, the intent log contains
# a complete set of deltas to replay.
#
log_must zfs unmount /$TESTPOOL/$TESTFS

log_note "Verify transactions to replay:"
log_must zdb -iv $TESTPOOL/$TESTFS

log_must zpool export $TESTPOOL

#
# 6. Remount TESTFS <which replays the intent log>
#
# Import the pool to unfreeze it and claim log blocks.  It has to be
# `zpool import -f` because we can't write a frozen pool's labels!
#
log_must zpool import -f -d $VDIR $TESTPOOL

#
# 7. Compare TESTFS against the TESTDIR/copy
#
log_note "Verify current block usage:"
log_must zdb -bcv $TESTPOOL

log_note "Verify copy of xattrs:"
log_must ls_xattr /$TESTPOOL/$TESTFS/xattr.dir
log_must ls_xattr /$TESTPOOL/$TESTFS/xattr.file

log_note "Verify working set diff:"
log_must replay_directory_diff $TESTDIR/copy /$TESTPOOL/$TESTFS

log_note "Verify file checksum:"
typeset checksum1=$(sha256digest /$TESTPOOL/$TESTFS/payload)
[[ "$checksum1" == "$checksum" ]] || \
    log_fail "checksum mismatch ($checksum1 != $checksum)"

log_pass "Replay of intent log succeeds."
