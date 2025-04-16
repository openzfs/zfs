#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
#
#
# STRATEGY:
# 1. Create a new encrypted filesystem
# 2. Add a 4 files that are to be truncated later
# 3. Take a snapshot of the filesystem
# 4. Truncate one of the files from 32M to 128k
# 5. Truncate one of the files from 512k to 384k
# 6. Truncate one of the files from 512k to 0 to 384k via reallocation
# 7. Truncate one of the files from 1k to 0 to 512b via reallocation
# 8. Take another snapshot of the filesystem
# 9. Send and receive both snapshots
# 10. Mount the filesystem and check the contents
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

function recursive_cksum
{
	find $1 -type f -exec xxh128sum {} + | \
	    sort -k 2 | awk '{ print $1 }' | xxh128digest
}

log_assert "Verify 'zfs send -w' works with many different file layouts"

typeset keyfile=/$TESTPOOL/pkey
typeset sendfile=/$TESTPOOL/sendfile
typeset sendfile2=/$TESTPOOL/sendfile2

# Create an encrypted dataset
log_must eval "echo 'password' > $keyfile"
log_must zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=file://$keyfile $TESTPOOL/$TESTFS2

# Explicitly set the recordsize since the truncation sizes below depend on
# this value being 128k.  This is currently same as the default recordsize.
log_must zfs set recordsize=128k $TESTPOOL/$TESTFS2

# Create files with varied layouts on disk
log_must mkfile 32M /$TESTPOOL/$TESTFS2/truncated
log_must mkfile 524288 /$TESTPOOL/$TESTFS2/truncated2
log_must mkfile 524288 /$TESTPOOL/$TESTFS2/truncated3
log_must mkfile 1024 /$TESTPOOL/$TESTFS2/truncated4

log_must zfs snapshot $TESTPOOL/$TESTFS2@snap1

#
# Truncate files created in the first snapshot. The first tests
# truncating a large file to a single block. The second tests
# truncating one block off the end of a file without changing
# the required nlevels to hold it. The third tests handling
# of a maxblkid that is dropped and then raised again. The
# fourth tests an object that is truncated from a single block
# to a smaller single block.
#
log_must truncate -s 131072 /$TESTPOOL/$TESTFS2/truncated
log_must truncate -s 393216 /$TESTPOOL/$TESTFS2/truncated2
log_must rm -f /$TESTPOOL/$TESTFS2/truncated3
log_must rm -f /$TESTPOOL/$TESTFS2/truncated4
sync_pool $TESTPOOL
log_must zfs umount $TESTPOOL/$TESTFS2
log_must zfs mount $TESTPOOL/$TESTFS2
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS2/truncated3 \
	bs=128k count=3 iflag=fullblock
log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS2/truncated4 \
	bs=512 count=1 iflag=fullblock

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

log_pass "Verified 'zfs send -w' works with many different file layouts"
