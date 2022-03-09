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
