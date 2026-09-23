#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
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
	log_must zpool create -f $TESTPOOL anymirror$parity $disks special mirror $sdisks
	log_must poolexists $TESTPOOL
	log_must zfs set special_small_blocks=4k $TESTPOOL

	log_must file_write -o create -f /$TESTPOOL/file.bin -b 1048576 -c 1
	log_must file_write -o create -f /$TESTPOOL/small.bin -b 4096 -c 1
	oldcksum=$(xxh128digest /$TESTPOOL/file.bin)
	oldsmallcksum=$(xxh128digest /$TESTPOOL/small.bin)
	log_must zpool export $TESTPOOL

	log_must zpool import -d $(dirname $disk0) $TESTPOOL
	newcksum=$(xxh128digest /$TESTPOOL/file.bin)
	newsmallcksum=$(xxh128digest /$TESTPOOL/small.bin)

	log_must test "$oldcksum" = "$newcksum"
	log_must test "$oldsmallcksum" = "$newsmallcksum"

	log_must destroy_pool $TESTPOOL
done

log_pass "Verify a variety of AnyRAID pools with a special VDEV mirror"
