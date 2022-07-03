#!/bin/ksh -p
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
# Verify that the dnode sizes of newly created files are consistent
# with the dnodesize dataset property.
#
# STRATEGY:
# 1. Create a file system
# 2. Set dnodesize to a legal literal value
# 3. Create a file
# 4. Repeat 2-3 for all legal literal values of dnodesize values
# 5. Unmount the file system
# 6. Use zdb to check expected dnode sizes
#

TEST_FS=$TESTPOOL/large_dnode

verify_runnable "both"

function cleanup
{
	datasetexists $TEST_FS && destroy_dataset $TEST_FS
}

log_onexit cleanup
log_assert "dnode sizes are consistent with dnodesize dataset property"

log_must zfs create $TEST_FS

set -A dnsizes "512" "1k" "2k" "4k" "8k" "16k"
set -A inodes

for ((i=0; i < ${#dnsizes[*]}; i++)) ; do
	size=${dnsizes[$i]}
	if [[ $size == "512" ]] ; then
		size="legacy"
	fi
	file=/$TEST_FS/file.$size
	log_must zfs set dnsize=$size $TEST_FS
	touch $file
	inodes[$i]=$(ls -li $file | awk '{print $1}')
done

log_must zfs umount $TEST_FS

for ((i=0; i < ${#dnsizes[*]}; i++)) ; do
	dnsize=$(zdb -dddd $TEST_FS ${inodes[$i]} |
	    awk '/ZFS plain file/ {gsub(/K/, "k", $6); print $6}')
	if [[ "$dnsize" != "${dnsizes[$i]}" ]]; then
		log_fail "dnode size is $dnsize (expected ${dnsizes[$i]})"
	fi
done

log_pass "dnode sizes are consistent with dnodesize dataset property"
