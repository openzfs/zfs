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
#	Deferred data blocks of many txgs get their tries together.  A file
#	that cannot be read defers blocks in each txg that wrote it.  If the
#	records of those txgs took their tries one after the other, the last
#	record would wait for the tries of every record before it, and the
#	holds of all of them would stay that long.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Write
#	   files a and wait until the lagging child has caught up.
#	2. Set 2 tries after the first, 3 s apart.  With the copy stopped,
#	   write file b in four txgs.  Export and import, so that the ARC
#	   does not hold b.  Inject read errors on the data of b that stay.
#	3. Restart the copy.  Wait until blocks are deferred, and then until
#	   no block is deferred.
#	4. Verify that the blocks are counted as lost, that nothing was
#	   recovered, and that the copy neither failed nor froze.  Clear the
#	   injection, write file c and wait until the lagging child has it.
#

verify_runnable "global"

log_onexit cleanup_async_mirror

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
log_must wait_async_mirror_idle

log_must async_mirror_set_tunable32 ASYNC_MIRROR_READ_RETRIES 2

# Write b in four txgs while the copy is stopped.
async_mirror_suspend_copy 1
async_mirror_write /$TESTPOOL/b 4
sync_pool $TESTPOOL
typeset -i i
for (( i = 1; i < 4; i++ )); do
	log_must file_write -o append -f /$TESTPOOL/b -b 131072 -c 4 -d R
	sync_pool $TESTPOOL
done
log_must zpool export $TESTPOOL
async_mirror_import
log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
log_must zinject -a -t data -e io -T read /$TESTPOOL/b

async_mirror_suspend_copy 0
log_must wait_async_mirror_stat data_deferred -gt 0
log_must wait_async_mirror_stat data_deferred -eq 0 \
	$(( 2 * ASYNC_MIRROR_TIMEOUT ))

log_must test "$(get_async_mirror_stat data_errors)" -ge 4
log_must test "$(get_async_mirror_stat data_recovered)" == 0
log_must test "$(get_async_mirror_stat failed)" == 0
log_must test "$(get_async_mirror_stat frozen)" == 0
log_must zinject -c all

async_mirror_write /$TESTPOOL/c 16
sync_pool $TESTPOOL
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat active)" == 1

log_pass "Deferred data blocks of many txgs get their tries together."
