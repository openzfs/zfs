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

log_assert "Verify configurations where multiple limit types are set"

iolimit_reset

log_must truncate -s 1G "$TESTDIR/file"

log_must zfs set iolimit_bw_read=4M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_write=5M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=6M "$TESTPOOL/$TESTFS"

log_must iolimit_bw_read 12 3 "$TESTDIR/file"
log_must iolimit_bw_write 15 3 "$TESTDIR/file"
stopwatch_start
ddio "$TESTDIR/file" "/dev/null" 36 &
ddio "/dev/zero" "$TESTDIR/file" 36 &
wait
stopwatch_check 12

log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_write=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS"

rm -f "$TESTDIR/file"

log_must create_dataset "$TESTPOOL/$TESTFS/lvl0"
log_must create_dataset "$TESTPOOL/$TESTFS/lvl0/lvl1"
log_must create_dataset "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2"

log_must truncate -s 1G "$TESTDIR/lvl0/lvl1/lvl2/file0"
log_must truncate -s 1G "$TESTDIR/lvl0/lvl1/lvl2/file1"

log_must zfs set iolimit_bw_read=4M "$TESTPOOL/$TESTFS/lvl0"
log_must zfs set iolimit_bw_write=5M "$TESTPOOL/$TESTFS/lvl0/lvl1"
log_must zfs set iolimit_bw_total=6M "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2"

log_must iolimit_bw_read 12 3 "$TESTDIR/lvl0/lvl1/lvl2/file0"
log_must iolimit_bw_write 15 3 "$TESTDIR/lvl0/lvl1/lvl2/file1"
stopwatch_start
ddio "$TESTDIR/lvl0/lvl1/lvl2/file0" "/dev/null" 36 &
ddio "/dev/zero" "$TESTDIR/lvl0/lvl1/lvl2/file1" 36 &
wait
stopwatch_check 12

log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/lvl0"
log_must zfs set iolimit_bw_write=none "$TESTPOOL/$TESTFS/lvl0/lvl1"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2"

log_must zfs set iolimit_bw_total=6M "$TESTPOOL/$TESTFS/lvl0"
log_must zfs set iolimit_bw_read=4M "$TESTPOOL/$TESTFS/lvl0/lvl1"
log_must zfs set iolimit_bw_write=5M "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2"

log_must iolimit_bw_read 12 3 "$TESTDIR/lvl0/lvl1/lvl2/file0"
log_must iolimit_bw_write 15 3 "$TESTDIR/lvl0/lvl1/lvl2/file1"
stopwatch_start
ddio "$TESTDIR/lvl0/lvl1/lvl2/file0" "/dev/null" 36 &
ddio "/dev/zero" "$TESTDIR/lvl0/lvl1/lvl2/file1" 36 &
wait
stopwatch_check 12

log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/lvl0"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/lvl0/lvl1"
log_must zfs set iolimit_bw_write=none "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2"

log_must destroy_dataset "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2"
log_must destroy_dataset "$TESTPOOL/$TESTFS/lvl0/lvl1"
log_must destroy_dataset "$TESTPOOL/$TESTFS/lvl0"

rm -f "$TESTDIR/lvl0/lvl1/lvl2/file0" "$TESTDIR/lvl0/lvl1/lvl2/file1"

log_pass
