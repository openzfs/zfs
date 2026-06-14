#! /bin/ksh -p
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
# Copyright (c) 2024 The FreeBSD Foundation
#
# This software was developed by Pawel Dawidek <pawel@dawidek.net>
# under sponsorship from the FreeBSD Foundation.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/iolimit/iolimit_common.kshlib

verify_runnable "both"

log_assert "Verify operations limits for multiple active process"

iolimit_reset

log_must touch "$TESTDIR/file"
log_must ln -s foo "$TESTDIR/symlink"

log_must iolimit_filesystem_op_read_multiple iolimit_op_read=none 1024 1
log_must iolimit_filesystem_op_read_multiple iolimit_op_read=128 512 8
log_must iolimit_filesystem_op_read_multiple iolimit_op_read=256 512 4
log_must iolimit_filesystem_op_read_multiple iolimit_op_read=512 1024 4
log_must iolimit_filesystem_op_read_multiple iolimit_op_read=none 1024 1

log_must iolimit_filesystem_op_read_multiple iolimit_op_total=none 1024 1
log_must iolimit_filesystem_op_read_multiple iolimit_op_total=128 512 8
log_must iolimit_filesystem_op_read_multiple iolimit_op_total=256 512 4
log_must iolimit_filesystem_op_read_multiple iolimit_op_total=512 1024 4
log_must iolimit_filesystem_op_read_multiple iolimit_op_total=none 1024 1

rm -f "$TESTDIR/file" "$TESTDIR/symlink"

log_must touch "$TESTDIR/file0" "$TESTDIR/file1" "$TESTDIR/file2" "$TESTDIR/file3" "$TESTDIR/file4"

log_must iolimit_filesystem_op_write_multiple_create iolimit_op_write=none 1024 1
log_must iolimit_filesystem_op_write_multiple_remove iolimit_op_write=none 1024 1
log_must iolimit_filesystem_op_write_multiple_create iolimit_op_write=128 128 5
log_must iolimit_filesystem_op_write_multiple_remove iolimit_op_write=128 128 5
log_must iolimit_filesystem_op_write_multiple_create iolimit_op_write=256 512 10
log_must iolimit_filesystem_op_write_multiple_remove iolimit_op_write=256 512 10
log_must iolimit_filesystem_op_write_multiple_create iolimit_op_write=none 1024 1
log_must iolimit_filesystem_op_write_multiple_remove iolimit_op_write=none 1024 1

log_must iolimit_filesystem_op_write_multiple_create iolimit_op_total=none 1024 1
log_must iolimit_filesystem_op_write_multiple_remove iolimit_op_total=none 1024 1
log_must iolimit_filesystem_op_write_multiple_create iolimit_op_total=128 128 5
log_must iolimit_filesystem_op_write_multiple_remove iolimit_op_total=128 128 5
log_must iolimit_filesystem_op_write_multiple_create iolimit_op_total=256 512 10
log_must iolimit_filesystem_op_write_multiple_remove iolimit_op_total=256 512 10
log_must iolimit_filesystem_op_write_multiple_create iolimit_op_total=none 1024 1
log_must iolimit_filesystem_op_write_multiple_remove iolimit_op_total=none 1024 1

rm -f "$TESTDIR/file0" "$TESTDIR/file1" "$TESTDIR/file2" "$TESTDIR/file3" "$TESTDIR/file4"

log_pass
