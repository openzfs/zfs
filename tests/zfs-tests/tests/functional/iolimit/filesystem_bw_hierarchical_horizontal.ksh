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

log_assert "Verify hierarchical limits for multiple file systems at the same level"

iolimit_reset

log_must create_dataset "$TESTPOOL/$TESTFS/foo"
log_must create_dataset "$TESTPOOL/$TESTFS/bar"
log_must create_dataset "$TESTPOOL/$TESTFS/baz"

log_must truncate -s 1G "$TESTDIR/file"
log_must truncate -s 1G "$TESTDIR/foo/file"
log_must truncate -s 1G "$TESTDIR/bar/file"
log_must truncate -s 1G "$TESTDIR/baz/file"

log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/baz"
log_must iolimit_bw_read 6 6 "$TESTDIR/foo/file"
log_must iolimit_bw_read 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file"
log_must iolimit_bw_read 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"
log_must iolimit_bw_read 6 6 "$TESTDIR/file" "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"

log_must zfs set iolimit_bw_read=2M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_read=1M "$TESTPOOL/$TESTFS/baz"
log_must iolimit_bw_read 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file"
log_must iolimit_bw_read 6 9 "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"
log_must iolimit_bw_read 6 12 "$TESTDIR/file" "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"

log_must zfs set iolimit_bw_read=2M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_read=2M "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_read=2M "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_read=2M "$TESTPOOL/$TESTFS/baz"
log_must iolimit_bw_read 6 3 "$TESTDIR/foo/file"
log_must iolimit_bw_read 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file"
log_must iolimit_bw_read 6 9 "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"
log_must iolimit_bw_read 6 12 "$TESTDIR/file" "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"

log_must zfs set iolimit_bw_read=2M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/baz"
log_must iolimit_bw_read 6 3 "$TESTDIR/foo/file"
log_must iolimit_bw_read 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file"
log_must iolimit_bw_read 6 9 "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"
log_must iolimit_bw_read 6 12 "$TESTDIR/file" "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"

log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/baz"

log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=1M "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_total=1M "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_total=1M "$TESTPOOL/$TESTFS/baz"
log_must iolimit_bw_read 6 6 "$TESTDIR/foo/file"
log_must iolimit_bw_read 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file"
log_must iolimit_bw_read 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"
log_must iolimit_bw_read 6 6 "$TESTDIR/file" "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"

log_must zfs set iolimit_bw_total=2M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=1M "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_total=1M "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_total=1M "$TESTPOOL/$TESTFS/baz"
log_must iolimit_bw_read 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file"
log_must iolimit_bw_read 6 9 "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"
log_must iolimit_bw_read 6 12 "$TESTDIR/file" "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"

log_must zfs set iolimit_bw_total=2M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=2M "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_total=2M "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_total=2M "$TESTPOOL/$TESTFS/baz"
log_must iolimit_bw_read 6 3 "$TESTDIR/foo/file"
log_must iolimit_bw_read 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file"
log_must iolimit_bw_read 6 9 "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"
log_must iolimit_bw_read 6 12 "$TESTDIR/file" "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"

log_must zfs set iolimit_bw_total=2M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/baz"
log_must iolimit_bw_read 6 3 "$TESTDIR/foo/file"
log_must iolimit_bw_read 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file"
log_must iolimit_bw_read 6 9 "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"
log_must iolimit_bw_read 6 12 "$TESTDIR/file" "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"

log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/baz"

log_must zfs set iolimit_bw_write=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_write=1M "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_write=1M "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_write=1M "$TESTPOOL/$TESTFS/baz"
log_must iolimit_bw_write 6 6 "$TESTDIR/foo/file"
log_must iolimit_bw_write 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file"
log_must iolimit_bw_write 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"
log_must iolimit_bw_write 6 6 "$TESTDIR/file" "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"

log_must zfs set iolimit_bw_write=2M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_write=1M "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_write=1M "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_write=1M "$TESTPOOL/$TESTFS/baz"
log_must iolimit_bw_write 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file"
log_must iolimit_bw_write 6 9 "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"
log_must iolimit_bw_write 6 12 "$TESTDIR/file" "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"

log_must zfs set iolimit_bw_write=2M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_write=2M "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_write=2M "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_write=2M "$TESTPOOL/$TESTFS/baz"
log_must iolimit_bw_write 6 3 "$TESTDIR/foo/file"
log_must iolimit_bw_write 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file"
log_must iolimit_bw_write 6 9 "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"
log_must iolimit_bw_write 6 12 "$TESTDIR/file" "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"

log_must zfs set iolimit_bw_write=2M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_write=none "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_write=none "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_write=none "$TESTPOOL/$TESTFS/baz"
log_must iolimit_bw_write 6 3 "$TESTDIR/foo/file"
log_must iolimit_bw_write 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file"
log_must iolimit_bw_write 6 9 "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"
log_must iolimit_bw_write 6 12 "$TESTDIR/file" "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"

log_must zfs set iolimit_bw_write=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_write=none "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_write=none "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_write=none "$TESTPOOL/$TESTFS/baz"

log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=1M "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_total=1M "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_total=1M "$TESTPOOL/$TESTFS/baz"
log_must iolimit_bw_write 6 6 "$TESTDIR/foo/file"
log_must iolimit_bw_write 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file"
log_must iolimit_bw_write 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"
log_must iolimit_bw_write 6 6 "$TESTDIR/file" "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"

log_must zfs set iolimit_bw_total=2M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=1M "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_total=1M "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_total=1M "$TESTPOOL/$TESTFS/baz"
log_must iolimit_bw_write 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file"
log_must iolimit_bw_write 6 9 "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"
log_must iolimit_bw_write 6 12 "$TESTDIR/file" "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"

log_must zfs set iolimit_bw_total=2M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=2M "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_total=2M "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_total=2M "$TESTPOOL/$TESTFS/baz"
log_must iolimit_bw_write 6 3 "$TESTDIR/foo/file"
log_must iolimit_bw_write 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file"
log_must iolimit_bw_write 6 9 "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"
log_must iolimit_bw_write 6 12 "$TESTDIR/file" "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"

log_must zfs set iolimit_bw_total=2M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/baz"
log_must iolimit_bw_write 6 3 "$TESTDIR/foo/file"
log_must iolimit_bw_write 6 6 "$TESTDIR/foo/file" "$TESTDIR/bar/file"
log_must iolimit_bw_write 6 9 "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"
log_must iolimit_bw_write 6 12 "$TESTDIR/file" "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"

log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/bar"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/baz"

log_must destroy_dataset "$TESTPOOL/$TESTFS/foo"
log_must destroy_dataset "$TESTPOOL/$TESTFS/bar"
log_must destroy_dataset "$TESTPOOL/$TESTFS/baz"

rm -f "$TESTDIR/file" "$TESTDIR/foo/file" "$TESTDIR/bar/file" "$TESTDIR/baz/file"

log_pass
