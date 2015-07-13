#!/bin/bash
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License, Version 1.0 only
# (the "License").  You may not use this file except in compliance
# with the License.
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
# Linux version
#
# To run just type ziltest.sh
#
# - creates a 200MB pool in /var/tmp/
# - prints information on:
#	working set files
#	ZIL records written
#	ZIL block usage
#	verification results
# - returns status of 0 on success
#
##########################################################################
#
# Here's how it all works:
#
# The general idea is to build up
# an intent log from a bunch of diverse user commands
# without actually committing them to the file system.
# Then copy the file system, replay the intent
# log and compare the file system and the copy.
#
# To enable this automated testing of the intent log
# requires some but minimal support from the file system.
# In particular, a "freeze" command is required to flush
# the in-flight transactions; to stop the actual
# committing of transactions; and to ensure no deltas are
# discarded. All deltas past a freeze point are kept for
# replay and comparison later. Here is the flow:
#
#	create an empty file system (FS)
#	freeze FS
#	run various user commands that create files, directories and ACLs
#	copy FS to temporary location (COPY)
#	unmount filesystem
#	<at this stage FS is empty again and unfrozen, and the
#	 intent log contains a complete set of deltas to replay it>
#	remount FS <which replays the intent log>
#	compare FS against the COPY
#

PATH=/usr/bin
PATH=$PATH:/usr/sbin
PATH=$PATH:/bin
PATH=$PATH:/sbin
export PATH

# ====================================================================
# SETUP
# ====================================================================
CMD=$(basename "$0")
POOL=ziltestpool.$$
DEVSIZE=${DEVSIZE-200m}
POOLDIR=/var/tmp
POOLFILE=$POOLDIR/ziltest_poolfile.$$
SLOGFILE=$POOLDIR/ziltest_slog.$$
FS=$POOL/fs
ROOT=/$FS
COPY=/var/tmp/${POOL}
KEEP=no

cleanup()
{
	zfs destroy -rf $FS
	echo "$CMD: pool I/O summary & status:"
	echo "----------------------------------------------------"
	zpool iostat $POOL
	echo
	zpool status $POOL
	echo "----------------------------------------------------"
	echo
	zpool destroy -f $POOL
	rm -rf $COPY
	rm $POOLFILE $SLOGFILE
}

bail()
{
	test $KEEP = no && cleanup
	echo "$1"
	exit 1
}

test $# -eq 0 || bail "usage: $CMD"

# ====================================================================
# PREP
#
# Create a pool using a file based vdev
# Create a destination for runtime copy of FS
# Freeze transaction syncing in the pool
# ====================================================================
truncate -s "$DEVSIZE" $POOLFILE || bail "can't make $POOLFILE"
truncate -s "$DEVSIZE" $SLOGFILE || bail "can't make $SLOGFILE"
zpool create $POOL $POOLFILE log $SLOGFILE || bail "can't create pool
$POOL"
zpool list $POOL

zfs set compression=on $POOL || bail "can't enable compression on $POOL"
zfs create $FS || bail "can't create $FS"
mkdir -p $COPY || bail "can't create $COPY"

#
# This dd command works around an issue where ZIL records aren't created
# after freezing the pool unless a ZIL header already exists. Create a file
# synchronously to force ZFS to write one out.
#
dd if=/dev/zero of=$ROOT/sync conv=fdatasync,fsync bs=1 count=1 2> /dev/null

zpool freeze $POOL || bail "can't freeze $POOL"

# ====================================================================
# TESTS
#
# Add operations here that will add commit records to the ZIL
#
# Use $ROOT for all file name prefixes
# ====================================================================

#
# TX_CREATE
#
touch $ROOT/a

#
# TX_RENAME
#
mv $ROOT/a $ROOT/b

#
# TX_SYMLINK
#
touch $ROOT/c
ln -s $ROOT/c $ROOT/d

#
# TX_LINK
#
touch $ROOT/e
ln $ROOT/e $ROOT/f

#
# TX_MKDIR
#
mkdir $ROOT/dir_to_delete

#
# TX_RMDIR
#
rmdir $ROOT/dir_to_delete

#
# Create a simple validation payload
#
PAYLOAD=$(modinfo -F filename zfs)
cp "$PAYLOAD" "$ROOT/payload"
CHECKSUM_BEFORE=$(sha256sum -b "$PAYLOAD")

#
# TX_WRITE (small file with ordering)
#
cp /etc/mtab $ROOT/small_file
cp /etc/profile $ROOT/small_file

#
# TX_CREATE, TX_MKDIR, TX_REMOVE, TX_RMDIR
#
cp -R /usr/share/dict $ROOT
rm -rf $ROOT/dict

#
# TX_SETATTR
#
touch $ROOT/setattr
chmod 567 $ROOT/setattr
chgrp root $ROOT/setattr
touch -cm -t 201311271200 $ROOT/setattr

#
# TX_TRUNCATE (to zero)
#
cp /etc/services $ROOT/truncated_file
> $ROOT/truncated_file

#
# Write to an open but removed file
#
(sleep 2; date) > $ROOT/date & sleep 1; rm $ROOT/date; wait

#
# TX_WRITE (large file)
#
dd if=/usr/share/lib/termcap of=$ROOT/large bs=128k oflag=sync 2> /dev/null

#
# Write zeroes, which compresss to holes, in the middle of a file
#
dd if=$POOLFILE of=$ROOT/holes.1 bs=128k count=8 2> /dev/null
dd if=/dev/zero of=$ROOT/holes.1 bs=128k count=2 2> /dev/null

dd if=$POOLFILE of=$ROOT/holes.2 bs=128k count=8 2> /dev/null
dd if=/dev/zero of=$ROOT/holes.2 bs=128k count=2 oseek=2 2> /dev/null

dd if=$POOLFILE of=$ROOT/holes.3 bs=128k count=8 2> /dev/null
dd if=/dev/zero of=$ROOT/holes.3 bs=128k count=2 oseek=2 conv=notrunc 2> /dev/null

#
# TX_MKXATTR
#
mkdir $ROOT/xattr.dir
attr -qs fileattr -V HelloWorld $ROOT/xattr.dir
attr -qs tmpattr -V HelloWorld $ROOT/xattr.dir
attr -qr tmpattr $ROOT/xattr.dir

touch $ROOT/xattr.file
attr -qs fileattr -V HelloWorld $ROOT/xattr.file
attr -qs tmpattr -V HelloWorld $ROOT/xattr.file
attr -qr tmpattr $ROOT/xattr.file
rm $ROOT/xattr.file


# ====================================================================
# REPLAY
# ====================================================================

KEEP=yes	# keep stuff around if we fail, so we can look at it

cd $ROOT
find . | cpio -pdmu --quiet $COPY
echo
cd /

zfs unmount $FS || bail "can't unmount $FS"

echo "$CMD: transactions to replay:"
echo "----------------------------------------------------"
zdb -ivv $FS || bail "can't run zdb on $POOL"
echo "----------------------------------------------------"
echo

#
# Export and reimport the pool to unfreeze it and claim log blocks.
# It has to be import -f because we can't write a frozen pool's labels!
#
zpool export $POOL || bail "can't export $POOL"
zpool import -f -d $POOLDIR $POOL || bail "can't import $POOL"

# ====================================================================
# VERIFY
# ====================================================================

echo "$CMD: current block usage:"
echo "----------------------------------------------------"
zdb -bcv $POOL || bail "blocks were leaked!"
echo "----------------------------------------------------"
echo

echo "$CMD: Copy of xattrs:"
echo "----------------------------------------------------"
attr -l $ROOT/xattr.dir || bail "can't list xattrs"
echo "----------------------------------------------------"
echo

echo "$CMD: Results of workingset diff:"
echo "----------------------------------------------------"
diff -r $ROOT $COPY > /dev/null || diff -r $ROOT $COPY || bail "replay diffs!"

echo "$CHECKSUM_BEFORE" | sha256sum -c || bail "payload checksums don't match"
echo "payload checksum matched"
echo "----------------------------------------------------"
echo

cleanup

exit 0
