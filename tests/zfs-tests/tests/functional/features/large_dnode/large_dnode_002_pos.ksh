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
# Copyright (c) 2016 by Lawrence Livermore National Security, LLC.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that extended attributes can use extra bonus space of a large
# dnode without kicking in a spill block.
#
# STRATEGY:
# 1. Create a file system with xattr=sa
# 2. Set dnodesize to a legal literal value
# 3. Create a file
# 4  Store an xattr that fits within the dnode size
# 4. Repeat 2-3 for all legal literal values of dnodesize values
# 5. Unmount the file system
# 6. Use zdb to check for missing SPILL_BLKPTR flag
#

TEST_FS=$TESTPOOL/large_dnode

verify_runnable "both"

function cleanup
{
	datasetexists $TEST_FS && destroy_dataset $TEST_FS
}

log_onexit cleanup
log_assert "extended attributes use extra bonus space of a large dnode"

log_must zfs create -o xattr=sa $TEST_FS

# Store dnode size minus 512 in an xattr
set -A xattr_sizes "512" "1536" "3584" "7680" "15872"
set -A prop_values "1k"  "2k"   "4k"   "8k"   "16k"
set -A inodes

for ((i=0; i < ${#prop_values[*]}; i++)) ; do
	prop_val=${prop_values[$i]}
	file=/$TEST_FS/file.$prop_val
	log_must zfs set dnsize=$prop_val $TEST_FS
	touch $file
	xattr_size=${xattr_sizes[$i]}
	xattr_name=user.foo
	xattr_val=$(dd if=/dev/urandom bs=1 count=$xattr_size |
	    openssl enc -a -A)
	log_must setfattr -n $xattr_name -v 0s$xattr_val $file
	inodes[$i]=$(ls -li $file | awk '{print $1}')
done

log_must zfs umount $TEST_FS

for ((i=0; i < ${#inodes[*]}; i++)) ; do
	log_mustnot eval "zdb -dddd $TEST_FS ${inodes[$i]} | grep SPILL_BLKPTR"
done

log_pass "extended attributes use extra bonus space of a large dnode"
