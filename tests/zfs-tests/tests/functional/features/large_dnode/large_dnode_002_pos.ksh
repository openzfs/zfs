#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
