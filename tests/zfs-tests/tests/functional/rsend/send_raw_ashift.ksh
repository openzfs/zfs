#!/bin/ksh

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
. $STF_SUITE/include/properties.shlib
. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# Description:
# Verify encrypted raw sending to pools with greater ashift succeeds.
#
# Strategy:
# 1) Create a set of files each containing some file data in an
#	encrypted filesystem.
# 2) Snapshot and raw send these files to a pool with greater ashift
# 3) Verify that all the xattrs (and thus the spill block) were
#    preserved when receiving the incremental stream.
# 4) Repeat the test for a non-encrypted filesystem using raw send
#

verify_runnable "both"

log_assert "Verify raw sending to pools with greater ashift succeeds"

if is_freebsd; then
	log_unsupported "Runs too long on FreeBSD 14 (Issue #14961)"
fi

function cleanup
{
	rm -f $BACKDIR/fs@*
	poolexists pool9 && destroy_pool pool9
	poolexists pool12 && destroy_pool pool12
	log_must rm -f $TESTDIR/vdev_a $TESTDIR/vdev_b
}

function xattr_test
{
	log_must zfs set xattr=sa pool9/$1
	log_must zfs set dnodesize=legacy pool9/$1
	log_must zfs set recordsize=128k pool9/$1
	rand_set_prop pool9/$1 compression "${compress_prop_vals[@]}"

	# Create 40 files each with a spill block containing xattrs.  Each file
	# will be modified in a different way to validate the incremental receive.
	for i in {1..40}; do
		file="/pool9/$1/file$i"

		log_must mkfile 16384 $file
		for j in {1..20}; do
			log_must set_xattr "testattr$j" "$attrvalue" $file
		done
	done

	# Snapshot the pool and send it to the new dataset.
	log_must zfs snapshot pool9/$1@snap1
	log_must eval "zfs send -w pool9/$1@snap1 >$BACKDIR/$1@snap1"
	log_must eval "zfs recv pool12/$1 < $BACKDIR/$1@snap1"

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
	log_must mkfile 32768 /pool9/$1/file1
	log_must mkfile 1048576 /pool9/$1/file2
	log_must truncate -s 0 /pool9/$1/file3
	log_must truncate -s 8192 /pool9/$1/file4
	log_must truncate -s 1073741824 /pool9/$1/file5
	log_must truncate -s 50 /pool9/$1/file6

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
	log_must mkfile 32768 /pool9/$1/file11
	log_must mkfile 1048576 /pool9/$1/file12
	log_must truncate -s 0 /pool9/$1/file13
	log_must truncate -s 8192 /pool9/$1/file14
	log_must truncate -s 1073741824 /pool9/$1/file15
	log_must truncate -s 50 /pool9/$1/file16

	for i in {11..20}; do
		log_must rm_xattr testattr1 /pool9/$1/file$i
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
	log_must mkfile 32768 /pool9/$1/file21
	log_must mkfile 1048576 /pool9/$1/file22
	log_must truncate -s 0 /pool9/$1/file23
	log_must truncate -s 8192 /pool9/$1/file24
	log_must truncate -s 1073741824 /pool9/$1/file25
	log_must truncate -s 50 /pool9/$1/file26

	for i in {21..30}; do
		for j in {1..20}; do
			log_must rm_xattr testattr$j /pool9/$1/file$i
		done
	done

	#
	# Modify file[31-40]'s spill blocks but not the file contents.
	#
	for i in {31..40}; do
		file="/pool9/$1/file$i"
		log_must rm_xattr testattr$(((RANDOM % 20) + 1)) $file
		log_must set_xattr testattr$(((RANDOM % 20) + 1)) "$attrvalue" $file
	done

	# Snapshot the pool and send the incremental snapshot.
	log_must zfs snapshot pool9/$1@snap2
	log_must eval "zfs send -w -i pool9/$1@snap1 pool9/$1@snap2 >$BACKDIR/$1@snap2"
	log_must eval "zfs recv pool12/$1 < $BACKDIR/$1@snap2"
}

attrvalue="abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz"

log_onexit cleanup

# Create pools
truncate -s $MINVDEVSIZE $TESTDIR/vdev_a
truncate -s $MINVDEVSIZE $TESTDIR/vdev_b
log_must zpool create -f -o ashift=9 pool9 $TESTDIR/vdev_a
log_must zpool create -f -o ashift=12 pool12 $TESTDIR/vdev_b

# Create encrypted fs
log_must eval "echo 'password' | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt " \
	"pool9/encfs"

# Run xattr tests for encrypted fs
xattr_test encfs

# Calculate the expected recursive checksum for source encrypted fs
expected_cksum=$(recursive_cksum /pool9/encfs)

# Mount target encrypted fs
log_must eval "echo 'password' | zfs load-key pool12/encfs"
log_must zfs mount pool12/encfs

# Validate the received copy using the received recursive checksum
actual_cksum=$(recursive_cksum /pool12/encfs)
if [[ "$expected_cksum" != "$actual_cksum" ]]; then
	log_fail "Checksums differ ($expected_cksum != $actual_cksum)"
fi

# Perform the same test but without encryption (send -w)
log_must zfs create pool9/fs

# Run xattr tests for non-encrypted fs
xattr_test fs

# Calculate the expected recursive checksum for source non-encrypted fs
expected_cksum=$(recursive_cksum /pool9/fs)

# Validate the received copy using the received recursive checksum
actual_cksum=$(recursive_cksum /pool12/fs)
if [[ "$expected_cksum" != "$actual_cksum" ]]; then
	log_fail "Checksums differ ($expected_cksum != $actual_cksum)"
fi

log_pass "Verify raw sending to pools with greater ashift succeeds"
