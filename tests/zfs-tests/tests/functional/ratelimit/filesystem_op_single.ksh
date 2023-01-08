#! /bin/ksh -p
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
. $STF_SUITE/tests/functional/ratelimit/ratelimit_common.kshlib

verify_runnable "both"

log_assert "Verify operations limits for a single active process"

ratelimit_reset

log_must touch "$TESTDIR/file"
log_must ln -s foo "$TESTDIR/symlink"

# Operations read limits.
log_must ratelimit_filesystem_op_single stat limit_op_read=64 512 8 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single readlink limit_op_read=64 512 8 "$TESTDIR/symlink"
log_must ratelimit_filesystem_op_single stat limit_op_read=128 512 4 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single readlink limit_op_read=128 512 4 "$TESTDIR/symlink"
log_must ratelimit_filesystem_op_single stat limit_op_read=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single readlink limit_op_read=none 1024 1 "$TESTDIR/symlink"

# Operations total limits limit reading.
log_must ratelimit_filesystem_op_single stat limit_op_total=64 512 8 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single readlink limit_op_total=64 512 8 "$TESTDIR/symlink"
log_must ratelimit_filesystem_op_single stat limit_op_total=128 512 4 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single readlink limit_op_total=128 512 4 "$TESTDIR/symlink"
log_must ratelimit_filesystem_op_single stat limit_op_total=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single readlink limit_op_total=none 1024 1 "$TESTDIR/symlink"

# Operations write limits don't affect reading.
log_must ratelimit_filesystem_op_single stat limit_op_write=64 512 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single readlink limit_op_write=64 512 1 "$TESTDIR/symlink"
log_must ratelimit_filesystem_op_single stat limit_op_write=128 512 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single readlink limit_op_write=128 512 1 "$TESTDIR/symlink"
log_must ratelimit_filesystem_op_single stat limit_op_write=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single readlink limit_op_write=none 1024 1 "$TESTDIR/symlink"

# Operations write limits.
log_must ratelimit_filesystem_op_single chmod limit_op_write=64 512 8 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single chown limit_op_write=128 512 4 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single create limit_op_write=64 512 8 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single unlink limit_op_write=128 512 4 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single mkdir limit_op_write=64 512 8 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single rmdir limit_op_write=128 512 4 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single rename limit_op_write=64 512 8 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single link limit_op_write=128 512 4 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single unlink limit_op_write=64 512 8 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single symlink limit_op_write=128 512 4 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single unlink limit_op_write=64 512 8 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single chmod limit_op_write=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single chown limit_op_write=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single create limit_op_write=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single unlink limit_op_write=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single mkdir limit_op_write=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single rmdir limit_op_write=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single rename limit_op_write=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single link limit_op_write=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single unlink limit_op_write=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single symlink limit_op_write=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single unlink limit_op_write=none 1024 1 "$TESTDIR/file"

# Operations total limits limit writing.
log_must ratelimit_filesystem_op_single chmod limit_op_total=128 512 4 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single chown limit_op_total=64 512 8 "$TESTDIR/file"
# Creating a file requires one metadata write and one metadata read operation.
# On successful open(2), zfs_freebsd_open() calls vnode_create_vobject()
# with size=0. If size=0, vnode_create_vobject() interprets this as not having
# the proper size and calls VOP_GETATTR().
if is_freebsd; then
	log_must ratelimit_filesystem_op_single create limit_op_total=128 512 8 "$TESTDIR/file"
else
	log_must ratelimit_filesystem_op_single create limit_op_total=128 512 4 "$TESTDIR/file"
fi
log_must ratelimit_filesystem_op_single unlink limit_op_total=64 512 8 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single mkdir limit_op_total=128 512 4 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single rmdir limit_op_total=64 512 8 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single rename limit_op_total=128 512 4 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single link limit_op_total=64 512 8 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single unlink limit_op_total=128 512 4 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single symlink limit_op_total=64 512 8 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single unlink limit_op_total=128 512 4 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single chmod limit_op_total=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single chown limit_op_total=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single create limit_op_total=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single unlink limit_op_total=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single mkdir limit_op_total=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single rmdir limit_op_total=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single rename limit_op_total=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single link limit_op_total=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single unlink limit_op_total=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single symlink limit_op_total=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single unlink limit_op_total=none 1024 1 "$TESTDIR/file"

# Operations read limits don't affect writing.
log_must ratelimit_filesystem_op_single chmod limit_op_read=32 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single chown limit_op_read=64 1024 1 "$TESTDIR/file"
if is_freebsd; then
	log_must ratelimit_filesystem_op_single create limit_op_read=128 1024 8 "$TESTDIR/file"
else
	log_must ratelimit_filesystem_op_single create limit_op_read=128 1024 1 "$TESTDIR/file"
fi
log_must ratelimit_filesystem_op_single unlink limit_op_read=256 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single mkdir limit_op_read=32 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single rmdir limit_op_read=64 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single rename limit_op_read=128 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single link limit_op_read=256 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single unlink limit_op_read=32 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single symlink limit_op_read=64 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single unlink limit_op_read=128 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single chmod limit_op_read=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single chown limit_op_read=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single create limit_op_read=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single unlink limit_op_read=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single mkdir limit_op_read=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single rmdir limit_op_read=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single rename limit_op_read=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single link limit_op_read=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single unlink limit_op_read=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single symlink limit_op_read=none 1024 1 "$TESTDIR/file"
log_must ratelimit_filesystem_op_single unlink limit_op_read=none 1024 1 "$TESTDIR/file"

rm -f "$TESTDIR/file" "$TESTDIR/symlink"

log_pass
