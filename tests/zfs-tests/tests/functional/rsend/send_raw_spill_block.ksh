#!/bin/ksh
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
# Copyright (c) 2019, Lawrence Livermore National Security, LLC.
# Copyright (c) 2021, George Amanakis. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify spill blocks are correctly preserved in raw sends.
#
# Strategy:
# 1) Create a set of files each containing some file data in an
#	encrypted filesystem.
# 2) Add enough xattrs to the file to require a spill block.
# 3) Snapshot and raw send these files to a new dataset.
# 4) Modify the files and spill blocks in a variety of ways.
# 5) Send the changes using an raw incremental send stream.
# 6) Verify that all the xattrs (and thus the spill block) were
#    preserved when receiving the incremental stream.
#

verify_runnable "both"

log_assert "Verify spill blocks are correctly preserved in raw sends"

function cleanup
{
	rm -f $BACKDIR/fs@*
	destroy_dataset $POOL/fs "-rR"
	destroy_dataset $POOL/newfs "-rR"
}

attrvalue="abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"

log_onexit cleanup

log_must eval "echo 'password' | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt " \
	"$POOL/fs"
log_must zfs set xattr=sa $POOL/fs
log_must zfs set dnodesize=legacy $POOL/fs
log_must zfs set recordsize=128k $POOL/fs

# Create 40 files each with a spill block containing xattrs.  Each file
# will be modified in a different way to validate the incremental receive.
for i in {1..40}; do
	file="/$POOL/fs/file$i"

	log_must mkfile 16384 $file
	for j in {1..20}; do
		log_must set_xattr "testattr$j" "$attrvalue" $file
	done
done

# Snapshot the pool and send it to the new dataset.
log_must zfs snapshot $POOL/fs@snap1
log_must eval "zfs send -w $POOL/fs@snap1 >$BACKDIR/fs@snap1"
log_must eval "zfs recv $POOL/newfs < $BACKDIR/fs@snap1"

#
# Modify file[1-6]'s contents but not the spill blocks.
#
# file1 - Increase record size; single block
# file2 - Increase record size; multiple blocks
# file3 - Truncate file to zero size; single block
# file4 - Truncate file to smaller size; single block
# file5 - Truncate file to much larger size; add holes
# file6 - Truncate file to embedded size; embedded data
#
log_must mkfile 32768 /$POOL/fs/file1
log_must mkfile 1048576 /$POOL/fs/file2
log_must truncate -s 0 /$POOL/fs/file3
log_must truncate -s 8192 /$POOL/fs/file4
log_must truncate -s 1073741824 /$POOL/fs/file5
log_must truncate -s 50 /$POOL/fs/file6

#
# Modify file[11-16]'s contents and their spill blocks.
#
# file11 - Increase record size; single block
# file12 - Increase record size; multiple blocks
# file13 - Truncate file to zero size; single block
# file14 - Truncate file to smaller size; single block
# file15 - Truncate file to much larger size; add holes
# file16 - Truncate file to embedded size; embedded data
#
log_must mkfile 32768 /$POOL/fs/file11
log_must mkfile 1048576 /$POOL/fs/file12
log_must truncate -s 0 /$POOL/fs/file13
log_must truncate -s 8192 /$POOL/fs/file14
log_must truncate -s 1073741824 /$POOL/fs/file15
log_must truncate -s 50 /$POOL/fs/file16

for i in {11..20}; do
	log_must rm_xattr testattr1 /$POOL/fs/file$i
done

#
# Modify file[21-26]'s contents and remove their spill blocks.
#
# file21 - Increase record size; single block
# file22 - Increase record size; multiple blocks
# file23 - Truncate file to zero size; single block
# file24 - Truncate file to smaller size; single block
# file25 - Truncate file to much larger size; add holes
# file26 - Truncate file to embedded size; embedded data
#
log_must mkfile 32768 /$POOL/fs/file21
log_must mkfile 1048576 /$POOL/fs/file22
log_must truncate -s 0 /$POOL/fs/file23
log_must truncate -s 8192 /$POOL/fs/file24
log_must truncate -s 1073741824 /$POOL/fs/file25
log_must truncate -s 50 /$POOL/fs/file26

for i in {21..30}; do
	for j in {1..20}; do
		log_must rm_xattr testattr$j /$POOL/fs/file$i
	done
done

#
# Modify file[31-40]'s spill blocks but not the file contents.
#
for i in {31..40}; do
	file="/$POOL/fs/file$i"
	log_must rm_xattr testattr$(((RANDOM % 20) + 1)) $file
	log_must set_xattr testattr$(((RANDOM % 20) + 1)) "$attrvalue" $file
done

# Calculate the expected recursive checksum for the source.
expected_cksum=$(recursive_cksum /$POOL/fs)

# Snapshot the pool and send the incremental snapshot.
log_must zfs snapshot $POOL/fs@snap2
log_must eval "zfs send -w -i $POOL/fs@snap1 $POOL/fs@snap2 >$BACKDIR/fs@snap2"
log_must eval "zfs recv $POOL/newfs < $BACKDIR/fs@snap2"
log_must eval "echo 'password' | zfs load-key $POOL/newfs"
log_must zfs mount $POOL/newfs

# Validate the received copy using the received recursive checksum.
actual_cksum=$(recursive_cksum /$POOL/newfs)
if [[ "$expected_cksum" != "$actual_cksum" ]]; then
	log_fail "Checksums differ ($expected_cksum != $actual_cksum)"
fi

log_pass "Verify spill blocks are correctly preserved in raw sends"
