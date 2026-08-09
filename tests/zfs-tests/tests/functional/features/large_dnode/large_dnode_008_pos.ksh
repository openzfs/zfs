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
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
# Use is subject to license terms.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Run many xattrtests on a dataset with large dnodes and xattr=sa to
# stress concurrent allocation of large dnodes.
#

TEST_FS=$TESTPOOL/large_dnode

verify_runnable "both"

function cleanup
{
	datasetexists $TEST_FS && destroy_dataset $TEST_FS
}

function verify_dnode_packing
{
	zdb -dd $TEST_FS | grep -A 3 'Dnode slots' | awk '
		/Total used:/ {total_used=$NF}
		/Max used:/ {max_used=$NF}
		/Percent empty:/ {print total_used, max_used, int($NF)}
	' | while read total_used max_used pct_empty
	do
		log_note "total_used $total_used max_used $max_used pct_empty $pct_empty"
		if [ $pct_empty -gt 5 ]; then
			log_fail "Holes in dnode array: pct empty $pct_empty > 5"
		fi
	done
}

log_onexit cleanup
log_assert "xattrtest runs concurrently on dataset with large dnodes"

log_must zfs create $TEST_FS
log_must zfs set dnsize=auto $TEST_FS
log_must zfs set xattr=sa $TEST_FS

for ((i=0; i < 100; i++)); do
	dir="/$TEST_FS/dir.$i"
	log_must mkdir "$dir"
	log_must eval "xattrtest -R -r -y -x 1 -f 1024 -k -p $dir >/dev/null 2>&1 &"
done

log_must wait
sync_pool $TESTPOOL

verify_dnode_packing

log_pass
