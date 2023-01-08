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

log_assert "Verify operations limits for a single active process"

iolimit_reset

log_must touch "$TESTDIR/file"
log_must ln -s foo "$TESTDIR/symlink"

# Operations read limits.
log_must iolimit_filesystem_op_single stat iolimit_op_read=64 512 8 "$TESTDIR/file"
log_must iolimit_filesystem_op_single readlink iolimit_op_read=64 512 8 "$TESTDIR/symlink"
log_must iolimit_filesystem_op_single stat iolimit_op_read=128 512 4 "$TESTDIR/file"
log_must iolimit_filesystem_op_single readlink iolimit_op_read=128 512 4 "$TESTDIR/symlink"
log_must iolimit_filesystem_op_single stat iolimit_op_read=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single readlink iolimit_op_read=none 1024 1 "$TESTDIR/symlink"

# Operations total limits limit reading.
log_must iolimit_filesystem_op_single stat iolimit_op_total=64 512 8 "$TESTDIR/file"
log_must iolimit_filesystem_op_single readlink iolimit_op_total=64 512 8 "$TESTDIR/symlink"
log_must iolimit_filesystem_op_single stat iolimit_op_total=128 512 4 "$TESTDIR/file"
log_must iolimit_filesystem_op_single readlink iolimit_op_total=128 512 4 "$TESTDIR/symlink"
log_must iolimit_filesystem_op_single stat iolimit_op_total=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single readlink iolimit_op_total=none 1024 1 "$TESTDIR/symlink"

# Operations write limits don't affect reading.
log_must iolimit_filesystem_op_single stat iolimit_op_write=64 512 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single readlink iolimit_op_write=64 512 1 "$TESTDIR/symlink"
log_must iolimit_filesystem_op_single stat iolimit_op_write=128 512 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single readlink iolimit_op_write=128 512 1 "$TESTDIR/symlink"
log_must iolimit_filesystem_op_single stat iolimit_op_write=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single readlink iolimit_op_write=none 1024 1 "$TESTDIR/symlink"

# Operations write limits.
log_must iolimit_filesystem_op_single chmod iolimit_op_write=64 512 8 "$TESTDIR/file"
log_must iolimit_filesystem_op_single chown iolimit_op_write=128 512 4 "$TESTDIR/file"
log_must iolimit_filesystem_op_single create iolimit_op_write=64 512 8 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_write=128 512 4 "$TESTDIR/file"
log_must iolimit_filesystem_op_single mkdir iolimit_op_write=64 512 8 "$TESTDIR/file"
log_must iolimit_filesystem_op_single rmdir iolimit_op_write=128 512 4 "$TESTDIR/file"
log_must iolimit_filesystem_op_single rename iolimit_op_write=64 512 8 "$TESTDIR/file"
log_must iolimit_filesystem_op_single link iolimit_op_write=128 512 4 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_write=64 512 8 "$TESTDIR/file"
log_must iolimit_filesystem_op_single symlink iolimit_op_write=128 512 4 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_write=64 512 8 "$TESTDIR/file"
log_must iolimit_filesystem_op_single chmod iolimit_op_write=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single chown iolimit_op_write=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single create iolimit_op_write=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_write=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single mkdir iolimit_op_write=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single rmdir iolimit_op_write=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single rename iolimit_op_write=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single link iolimit_op_write=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_write=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single symlink iolimit_op_write=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_write=none 1024 1 "$TESTDIR/file"

# Operations total limits limit writing.
log_must iolimit_filesystem_op_single chmod iolimit_op_total=128 512 4 "$TESTDIR/file"
log_must iolimit_filesystem_op_single chown iolimit_op_total=64 512 8 "$TESTDIR/file"
log_must iolimit_filesystem_op_single create iolimit_op_total=128 512 4 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_total=64 512 8 "$TESTDIR/file"
log_must iolimit_filesystem_op_single mkdir iolimit_op_total=128 512 4 "$TESTDIR/file"
log_must iolimit_filesystem_op_single rmdir iolimit_op_total=64 512 8 "$TESTDIR/file"
log_must iolimit_filesystem_op_single rename iolimit_op_total=128 512 4 "$TESTDIR/file"
log_must iolimit_filesystem_op_single link iolimit_op_total=64 512 8 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_total=128 512 4 "$TESTDIR/file"
log_must iolimit_filesystem_op_single symlink iolimit_op_total=64 512 8 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_total=128 512 4 "$TESTDIR/file"
log_must iolimit_filesystem_op_single chmod iolimit_op_total=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single chown iolimit_op_total=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single create iolimit_op_total=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_total=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single mkdir iolimit_op_total=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single rmdir iolimit_op_total=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single rename iolimit_op_total=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single link iolimit_op_total=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_total=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single symlink iolimit_op_total=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_total=none 1024 1 "$TESTDIR/file"

# Operations read limits don't affect writing.
log_must iolimit_filesystem_op_single chmod iolimit_op_read=32 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single chown iolimit_op_read=64 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single create iolimit_op_read=128 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_read=256 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single mkdir iolimit_op_read=32 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single rmdir iolimit_op_read=64 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single rename iolimit_op_read=128 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single link iolimit_op_read=256 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_read=32 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single symlink iolimit_op_read=64 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_read=128 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single chmod iolimit_op_read=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single chown iolimit_op_read=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single create iolimit_op_read=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_read=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single mkdir iolimit_op_read=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single rmdir iolimit_op_read=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single rename iolimit_op_read=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single link iolimit_op_read=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_read=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single symlink iolimit_op_read=none 1024 1 "$TESTDIR/file"
log_must iolimit_filesystem_op_single unlink iolimit_op_read=none 1024 1 "$TESTDIR/file"

rm -f "$TESTDIR/file" "$TESTDIR/symlink"

log_pass
