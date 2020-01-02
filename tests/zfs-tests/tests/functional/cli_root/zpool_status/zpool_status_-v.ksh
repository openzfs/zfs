#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2019 Lawrence Livermore National Security, LLC.

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify correct output with 'zpool status -v' after corrupting a file
#
# STRATEGY:
# 1. Create a file
# 2. zinject checksum errors
# 3. Read the file
# 4. Verify we see "file corrupted" output in 'zpool status -v'
# 5. Verify we see one of the corrupted ranges in 'zpool status -vv'

verify_runnable "both"

log_assert "Verify correct 'zpool status -v' output with a corrupted file"
log_must mkfile 10m $TESTDIR/10m_file
log_must mkfile 1m $TESTDIR/1m_file

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

log_must zinject -t data -e checksum -f 100 $TESTDIR/10m_file
log_must zinject -t data -e checksum -f 100 $TESTDIR/1m_file

# Try to read '1m_file'.  It should stop after the first 128k block.
cat $TESTDIR/1m_file > /dev/null || true

# Try to read the 2nd megabyte of '10m_file'
dd if=$TESTDIR/10m_file bs=1M skip=1 count=1 || true

log_must zinject -c all

# Look to see that both our files report errors
log_must eval "zpool status -v | grep '10m_file: found'"
log_must eval "zpool status -v | grep '1m_file: found'"

# Look for one of our error ranges
log_must eval "zpool status -vv | grep '[0x100000-0x1fffff]'"

log_pass "'zpool status -vv' output is correct"
