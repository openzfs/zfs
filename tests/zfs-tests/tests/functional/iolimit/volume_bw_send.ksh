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

log_assert "Verify bandwidth limits for zfs send"

function iolimit_send
{
	typeset -r exp=$1

	stopwatch_start
	zfs send "$TESTPOOL/$TESTFS/foo@snap" >/dev/null
	stopwatch_check $exp
}

iolimit_reset

log_must create_volume "$TESTPOOL/$TESTFS/foo" 16M
log_must dd if=/dev/urandom of="/dev/zvol/$TESTPOOL/$TESTFS/foo" bs=1M count=16
log_must create_snapshot "$TESTPOOL/$TESTFS/foo" "snap"

log_must iolimit_send 1

log_must zfs set iolimit_bw_read=2M "$TESTPOOL/$TESTFS/foo"
log_must iolimit_send 8

log_must zfs set iolimit_bw_read=4M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_read=8M "$TESTPOOL/$TESTFS/foo"
log_must iolimit_send 4

log_must zfs set iolimit_bw_read=8M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_read=4M "$TESTPOOL/$TESTFS/foo"
log_must iolimit_send 4

log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/foo"
log_must iolimit_send 1

log_must zfs set iolimit_bw_total=2M "$TESTPOOL/$TESTFS/foo"
log_must iolimit_send 8

log_must zfs set iolimit_bw_total=4M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=8M "$TESTPOOL/$TESTFS/foo"
log_must iolimit_send 4

log_must zfs set iolimit_bw_total=8M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=4M "$TESTPOOL/$TESTFS/foo"
log_must iolimit_send 4

log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/foo"
log_must iolimit_send 1

log_must zfs set iolimit_bw_read=4M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_total=8M "$TESTPOOL/$TESTFS/foo"
log_must iolimit_send 4

log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=4M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_read=8M "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/foo"
log_must iolimit_send 4

log_must zfs set iolimit_bw_read=8M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_total=4M "$TESTPOOL/$TESTFS/foo"
log_must iolimit_send 4

log_must zfs set iolimit_bw_read=none "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_total=8M "$TESTPOOL/$TESTFS"
log_must zfs set iolimit_bw_read=4M "$TESTPOOL/$TESTFS/foo"
log_must zfs set iolimit_bw_total=none "$TESTPOOL/$TESTFS/foo"
log_must iolimit_send 4

destroy_snapshot "$TESTPOOL/$TESTFS/foo@snap"
log_must destroy_dataset "$TESTPOOL/$TESTFS/foo"

log_pass
