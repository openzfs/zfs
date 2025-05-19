#!/bin/ksh -p
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
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/tests/functional/anyraid/anyraid_common.kshlib

#
# DESCRIPTION:
# Verify a variety of AnyRAID pools with a special VDEV mirror.
#
# STRATEGY:
# 1. Create an AnyRAID pool with a special VDEV mirror.
# 2. Write to it, sync.
# 3. Export and re-import the pool.
# 4. Verify that all the file contents are unchanged on the file system.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}
log_onexit cleanup

log_assert "Verify a variety of AnyRAID pools with a special VDEV mirror"

log_must create_sparse_files "disk" 4 $DEVSIZE
log_must create_sparse_files "sdisk" 2 $DEVSIZE

typeset oldcksum
typeset newcksum
for parity in {0..3}; do
	log_must zpool create -f $TESTPOOL anyraid$parity $disks special mirror $sdisks
	log_must poolexists $TESTPOOL

	log_must dd if=/dev/urandom of=/$TESTPOOL/file.bin bs=1M count=128
	oldcksum=$(xxh128digest /$TESTPOOL/file.bin)
	log_must zpool export $TESTPOOL

	log_must zpool import -d $(dirname $disk0) $TESTPOOL
	newcksum=$(xxh128digest /$TESTPOOL/file.bin)

	log_must test "$oldcksum" = "$newcksum"

	log_must destroy_pool $TESTPOOL
done

log_pass "Verify a variety of AnyRAID pools with a special VDEV mirror"
