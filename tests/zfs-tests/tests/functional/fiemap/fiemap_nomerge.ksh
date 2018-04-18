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
# Copyright (c) 2018 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
#	Verify FIEMAP reports individual blocks when requested.
#
# STRATEGY:
#	1. Write a dense file and verify all blocks are reported.
#	2. Randomly write a sparse file with a known number of blocks,
#	   verify -a reports all blocks without merging.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/fiemap/fiemap.kshlib

verify_runnable "both"

log_assert "FIEMAP reports individual blocks when requested"
log_onexit fiemap_cleanup

BS=$(get_prop recordsize $TESTPOOL/$TESTFS)

log_note "Block count in a dense file"
fiemap_write $BS 32
fiemap_verify -D 0:$((BS*32)):1 -F "delalloc:all" -E 1
fiemap_verify -s -a -D 0:$((BS*32)):1 -E 32
fiemap_remove

log_note "Block count in a dense file with multiple copies"
log_must zfs set copies=2 $TESTPOOL/$TESTFS
fiemap_write $BS 32
fiemap_verify -D 0:$((BS*32)):1 -F "delalloc:all" -E 1
fiemap_verify -s -a -D 0:$((BS*32)):1 -E 32
fiemap_verify -a -c -D 0:$((BS*32)):2 -E 64
fiemap_remove
log_must zfs set copies=1 $TESTPOOL/$TESTFS

log_note "Block count in a sparse file"
all_blocks=""
for i in {0..99}; do
	all_blocks="$all_blocks\n$(($RANDOM % 200))"
done
unique_blocks=$(echo -e "$all_blocks" | sort -n | uniq -u | tr '\n' ' ')
unique_count=$(echo "$unique_blocks" | wc -w)

for block in $unique_blocks; do
	fiemap_write $BS 1 $block
done

fiemap_verify -a -E $unique_count -F "delalloc:all"
fiemap_verify -a -E $unique_count -F "merged:0"
fiemap_verify -s -a -E $unique_count -F "merged:0"
fiemap_remove

log_note "Block count in a sparse file with multiple copies"
all_blocks=""
for i in {0..99}; do
	all_blocks="$all_blocks\n$(($RANDOM % 200))"
done
unique_blocks=$(echo -e "$all_blocks" | sort -n | uniq -u | tr '\n' ' ')
unique_count=$(echo "$unique_blocks" | wc -w)

log_must zfs set copies=2 $TESTPOOL/$TESTFS
for block in $unique_blocks; do
	fiemap_write $BS 1 $block
done

fiemap_verify -a -E $unique_count -F "delalloc:all"
fiemap_verify -a -E $unique_count -F "merged:0"
fiemap_verify -s -a -E $unique_count -F "merged:0"
fiemap_verify -a -c -E $((unique_count*2)) -F "merged:0"
fiemap_remove

log_pass "FIEMAP reports individual blocks when requested"
