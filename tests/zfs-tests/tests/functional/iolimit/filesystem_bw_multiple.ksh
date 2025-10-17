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

log_assert "Verify bandwidth limits for multiple active processes"

iolimit_reset

log_must truncate -s 1G "$TESTDIR/file"

log_must iolimit_filesystem_bw_read_multiple iolimit_bw_read=none 500 1
log_must iolimit_filesystem_bw_read_multiple iolimit_bw_read=1M 5 15
log_must iolimit_filesystem_bw_read_multiple iolimit_bw_read=10M 50 15
log_must iolimit_filesystem_bw_read_multiple iolimit_bw_read=100M 500 15
log_must iolimit_filesystem_bw_read_multiple iolimit_bw_read=none 500 1

log_must iolimit_filesystem_bw_read_multiple iolimit_bw_total=none 500 1
log_must iolimit_filesystem_bw_read_multiple iolimit_bw_total=1M 5 15
log_must iolimit_filesystem_bw_read_multiple iolimit_bw_total=10M 50 15
log_must iolimit_filesystem_bw_read_multiple iolimit_bw_total=100M 500 15
log_must iolimit_filesystem_bw_read_multiple iolimit_bw_total=none 500 1

log_must iolimit_filesystem_bw_write_multiple iolimit_bw_write=none 500 1
log_must iolimit_filesystem_bw_write_multiple iolimit_bw_write=1M 5 15
log_must iolimit_filesystem_bw_write_multiple iolimit_bw_write=10M 50 15
log_must iolimit_filesystem_bw_write_multiple iolimit_bw_write=100M 500 15
log_must iolimit_filesystem_bw_write_multiple iolimit_bw_write=none 500 1

log_must iolimit_filesystem_bw_write_multiple iolimit_bw_total=none 500 1
log_must iolimit_filesystem_bw_write_multiple iolimit_bw_total=1M 5 15
log_must iolimit_filesystem_bw_write_multiple iolimit_bw_total=10M 50 15
log_must iolimit_filesystem_bw_write_multiple iolimit_bw_total=100M 500 15
log_must iolimit_filesystem_bw_write_multiple iolimit_bw_total=none 500 1

rm -f "$TESTDIR/file"

log_pass
