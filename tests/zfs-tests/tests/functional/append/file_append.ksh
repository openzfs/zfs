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
# Copyright (c) 2022 by Triad National Security, LCC
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Tests file offset using O_APPEND.
#
# STRATEGY:
# 1. Open file using O_APPEND
# 2. Write to the file using random number of blocks (1, 2, or 3)
# 3. Verify that the file offset is correct using lseek after the write
# 4. Repeat steps 2 and 3, 5 times
# 5. Close the file.
# 6. Repeat steps 1-5 but also open file with O_DIRECT
#

verify_runnable "global"

log_assert "Ensure file offset is updated correctly when opened with O_APPEND"

mntpt=$(get_prop mountpoint $TESTPOOL/$TESTFS)
filename=$mntpt/append_file.txt
bs=131072
ITERATIONS=5
expected=0

# First test using buffered writes with O_APPEND
for i in $(seq $ITERATIONS); do
	num_blocks=$(random_int_between 1 3) 
	expected=$((expected + ( bs * num_blocks)))
	log_must file_append -f $filename -e $expected -b $bs -n $num_blocks
	curr_offset=$expected
done

log_must rm -f $filename

expected=0

# Repeat same test using O_DIRECT writes with O_APPEND
for i in $(seq $ITERATIONS); do
	num_blocks=$(random_int_between 1 3) 
	expected=$((expected + ( bs * num_blocks)))
	log_must file_append -f $filename -e $expected -b $bs -n $num_blocks -d
done

log_must rm -f $filename

log_pass "File offset updated correctly when opening a file with O_APPEND."
