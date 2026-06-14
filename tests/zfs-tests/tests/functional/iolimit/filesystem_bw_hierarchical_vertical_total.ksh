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

log_assert "Verify hierarchical bandwidth total limits configured on multiple levels"

iolimit_reset

log_must create_dataset "$TESTPOOL/$TESTFS/lvl0"
log_must create_dataset "$TESTPOOL/$TESTFS/lvl0/lvl1"
log_must create_dataset "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2"

log_must truncate -s 1G "$TESTDIR/lvl0/lvl1/lvl2/file"

for lvl0 in none 1M 3M 5M; do
	for lvl1 in none 1M 3M 5M; do
		for lvl2 in none 1M 3M 5M; do
			# We need at least one level with 1M limit.
			if [ $lvl0 != "1M" ] && [ $lvl1 != "1M" ] && [ $lvl2 != "1M" ]; then
				continue
			fi

			log_must zfs set iolimit_bw_total=$lvl0 "$TESTPOOL/$TESTFS/lvl0"
			log_must zfs set iolimit_bw_total=$lvl1 "$TESTPOOL/$TESTFS/lvl0/lvl1"
			log_must zfs set iolimit_bw_total=$lvl2 "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2"
			log_must iolimit_bw_read 5 5 "$TESTDIR/lvl0/lvl1/lvl2/file"
			log_must iolimit_bw_write 5 5 "$TESTDIR/lvl0/lvl1/lvl2/file"
		done
	done
done

log_must destroy_dataset "$TESTPOOL/$TESTFS/lvl0/lvl1/lvl2"
log_must destroy_dataset "$TESTPOOL/$TESTFS/lvl0/lvl1"
log_must destroy_dataset "$TESTPOOL/$TESTFS/lvl0"

log_pass
