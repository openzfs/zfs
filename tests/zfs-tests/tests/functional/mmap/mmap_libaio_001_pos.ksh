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
# Copyright 2018 Canonical.  All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmap/mmap.cfg

#
# DESCRIPTION:
# Verify libaio functions correctly with mmap()'d files.
#
# STRATEGY:
# 1. Call mmap_libaio binary
# 2. Verify the file exists and is the expected size
# 3. Verify the filesystem is intact and not hung in any way
#

verify_runnable "global"

log_assert "verify mmap'd pages work with libaio"

# mmap_libaio is built when the libaio-devel package is installed.
command -v mmap_libaio > /dev/null || log_unsupported "This test requires mmap_libaio."

log_must chmod 777 $TESTDIR

for size in 512 4096 8192; do
	log_mustnot stat $TESTDIR/test-libaio-file
	log_must mmap_libaio $TESTDIR/test-libaio-file $size
	log_must verify_eq $(stat --format=%s $TESTDIR/test-libaio-file) $size
	log_must rm $TESTDIR/test-libaio-file
done

typeset dir=$(get_device_dir $DISKS)
verify_filesys "$TESTPOOL" "$TESTPOOL/$TESTFS" "$dir"

log_pass "mmap'd pages work with libaio"
