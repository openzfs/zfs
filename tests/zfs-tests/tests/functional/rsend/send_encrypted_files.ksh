#!/bin/ksh -p
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2018 by Datto Inc. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
# Verify that a raw zfs send and receive can deal with several different
# types of file layouts.
#
# STRATEGY:
# 1. Create a new encrypted filesystem
# 2. Add an empty file to the filesystem
# 3. Add a small 512 byte file to the filesystem
# 4. Add a larger 32M file to the filesystem
# 5. Add a large sparse file to the filesystem
# 6. Add 1000 empty files to the filesystem
# 7. Add a file with a large xattr value
# 8. Use xattrtest to create files with random xattrs (with and without xattrs=on)
# 9. Take a snapshot of the filesystem
# 10. Remove the 1000 empty files to the filesystem
# 11. Take another snapshot of the filesystem
# 12. Send and receive both snapshots
# 13. Mount the filesystem and check the contents
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS2 && \
		destroy_dataset $TESTPOOL/$TESTFS2 -r
	datasetexists $TESTPOOL/recv && \
		destroy_dataset $TESTPOOL/recv -r
	[[ -f $keyfile ]] && log_must rm $keyfile
	[[ -f $sendfile ]] && log_must rm $sendfile
}
log_onexit cleanup

log_assert "Verify 'zfs send -w' works with many different file layouts"

typeset keyfile=/$TESTPOOL/pkey
typeset sendfile=/$TESTPOOL/sendfile
typeset sendfile2=/$TESTPOOL/sendfile2

# Create an encrypted dataset
log_must eval "echo 'password' > $keyfile"
log_must zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=file://$keyfile $TESTPOOL/$TESTFS2

# Create files with varied layouts on disk
log_must touch /$TESTPOOL/$TESTFS2/empty
log_must mkfile 512 /$TESTPOOL/$TESTFS2/small
log_must mkfile 32M /$TESTPOOL/$TESTFS2/full
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS2/sparse \
	bs=512 count=1 seek=1048576 >/dev/null 2>&1

log_must mkdir -p /$TESTPOOL/$TESTFS2/dir
for i in {1..1000}; do
	log_must mkfile 512 /$TESTPOOL/$TESTFS2/dir/file-$i
done

log_must mkdir -p /$TESTPOOL/$TESTFS2/xattrondir
log_must zfs set xattr=on $TESTPOOL/$TESTFS2
log_must xattrtest -f 10 -x 3 -s 32768 -r -k -p /$TESTPOOL/$TESTFS2/xattrondir
log_must mkdir -p /$TESTPOOL/$TESTFS2/xattrsadir
log_must zfs set xattr=sa $TESTPOOL/$TESTFS2
log_must xattrtest -f 10 -x 3 -s 32768 -r -k -p /$TESTPOOL/$TESTFS2/xattrsadir

# OpenZFS issue #7432
log_must zfs set compression=on xattr=sa $TESTPOOL/$TESTFS2
log_must touch /$TESTPOOL/$TESTFS2/attrs
log_must eval "python3 -c 'print \"a\" * 4096' | \
	set_xattr_stdin bigval /$TESTPOOL/$TESTFS2/attrs"
log_must zfs set compression=off xattr=on $TESTPOOL/$TESTFS2

log_must zfs snapshot $TESTPOOL/$TESTFS2@snap1

# Remove the empty files created in the first snapshot
for i in {1..1000}; do
	log_must rm /$TESTPOOL/$TESTFS2/dir/file-$i
done
sync

log_must zfs snapshot $TESTPOOL/$TESTFS2@snap2
expected_cksum=$(recursive_cksum /$TESTPOOL/$TESTFS2)

log_must eval "zfs send -wp $TESTPOOL/$TESTFS2@snap1 > $sendfile"
log_must eval "zfs send -wp -i @snap1 $TESTPOOL/$TESTFS2@snap2 > $sendfile2"

log_must eval "zfs recv -F $TESTPOOL/recv < $sendfile"
log_must eval "zfs recv -F $TESTPOOL/recv < $sendfile2"
log_must zfs load-key $TESTPOOL/recv

log_must zfs mount -a
actual_cksum=$(recursive_cksum /$TESTPOOL/recv)
[[ "$expected_cksum" != "$actual_cksum" ]] && \
	log_fail "Recursive checksums differ ($expected_cksum != $actual_cksum)"

log_must xattrtest -f 10 -o3 -y -p /$TESTPOOL/recv/xattrondir
log_must xattrtest -f 10 -o3 -y -p /$TESTPOOL/recv/xattrsadir

log_pass "Verified 'zfs send -w' works with many different file layouts"
