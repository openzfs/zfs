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
# Copyright (c) 2026, John White.
#

. $STF_SUITE/tests/functional/async_mirror/async_mirror.kshlib

#
# DESCRIPTION:
#	An import reports no checksum errors on a lagging child that is
#	behind.  Without the fence, the import reads blocks from that child
#	that are newer than its txg, and each read fails its checksum.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.
#	2. Write files, and wait until the lagging child has caught up.
#	3. Stop the copy, write more files, and export the pool.
#	4. Import and export the pool five times.  Each time, read the
#	   files, and add up the checksum errors of the lagging child.
#	5. Verify that the sum is 0.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable

async_mirror_write_files old 2
log_must wait_async_mirror_idle
async_mirror_export_behind file 6

typeset -i i n sum=0

for (( i = 0; i < 5; i++ )); do
	async_mirror_import
	async_mirror_digests file 6 > /dev/null
	n=$(async_mirror_cksum_errors $ASYNC_MIRROR_LAGGING)
	log_note "Import $i: $n checksum errors on the lagging child"
	(( sum += n ))
	log_must zpool export $TESTPOOL
done
log_must test $sum -eq 0

log_pass "An import reports no checksum errors on a lagging child."
