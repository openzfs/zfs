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

log_must create_volume "$TESTPOOL/$TESTFS/vol0" 100M
log_must create_volume "$TESTPOOL/$TESTFS/vol1" 100M

log_must zfs set iolimit_bw_read=4M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_write=5M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=6M "$TESTPOOL/$TESTFS"

log_must iolimit_bw_read 12 3 "/dev/zvol/$TESTPOOL/$TESTFS/vol0"
log_must iolimit_bw_write 15 3 "/dev/zvol/$TESTPOOL/$TESTFS/vol1"
stopwatch_start
ddio "/dev/zvol/$TESTPOOL/$TESTFS/vol0" "/dev/null" 36 &
ddio "/dev/zero" "/dev/zvol/$TESTPOOL/$TESTFS/vol1" 36 &
wait
stopwatch_check 12

log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_write=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS"

log_must destroy_dataset "$TESTPOOL/$TESTFS/vol0"
log_must destroy_dataset "$TESTPOOL/$TESTFS/vol1"

log_must create_dataset "$TESTPOOL/$TESTFS/lvl0"
log_must create_dataset "$TESTPOOL/$TESTFS/lvl0/lvl1"
log_must create_dataset "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2"
log_must create_volume "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2/vol0" 100M
log_must create_volume "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2/vol1" 100M

log_must zfs set iolimit_bw_read=4M "$TESTPOOL/$TESTFS/lvl0"
log_must zfs set iolimit_bw_write=5M "$TESTPOOL/$TESTFS/lvl0/lvl1"
log_must zfs set iolimit_bw_total=6M "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2"

log_must iolimit_bw_read 12 3 "/dev/zvol/$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2/vol0"
log_must iolimit_bw_write 15 3 "/dev/zvol/$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2/vol1"
stopwatch_start
ddio "/dev/zvol/$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2/vol0" "/dev/null" 36 &
ddio "/dev/zero" "/dev/zvol/$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2/vol1" 36 &
wait
stopwatch_check 12

log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/lvl0"
log_must zfs set iolimit_bw_write=none "$TESTPOOL/$TESTFS/lvl0/lvl1"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2"

log_must zfs set iolimit_bw_total=6M "$TESTPOOL/$TESTFS/lvl0"
log_must zfs set iolimit_bw_read=4M "$TESTPOOL/$TESTFS/lvl0/lvl1"
log_must zfs set iolimit_bw_write=5M "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2"

log_must iolimit_bw_read 12 3 "/dev/zvol/$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2/vol0"
log_must iolimit_bw_write 15 3 "/dev/zvol/$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2/vol1"
stopwatch_start
ddio "/dev/zvol/$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2/vol0" "/dev/null" 36 &
ddio "/dev/zero" "/dev/zvol/$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2/vol1" 36 &
wait
stopwatch_check 12

log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/lvl0"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/lvl0/lvl1"
log_must zfs set iolimit_bw_write=none "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2"

log_must destroy_dataset "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2/vol1"
log_must destroy_dataset "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2/vol0"
log_must destroy_dataset "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2"
log_must destroy_dataset "$TESTPOOL/$TESTFS/lvl0/lvl1"
log_must destroy_dataset "$TESTPOOL/$TESTFS/lvl0"

log_pass
