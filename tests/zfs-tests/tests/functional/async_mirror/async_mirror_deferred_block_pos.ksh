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
#	A read error on a data block that lasts longer than the tries of the
#	copy defers the block, and the copy goes on.  The block is tried
#	again before its record is released.  When the error has passed by
#	then, the block is copied to the lagging child after all: no data
#	error is counted, and an async_mirror_recovered event names it.
#	When the error stays, the block is reported as lost, as before, and
#	the copy goes on.  While deferred blocks wait for their tries, a
#	lost data block of a later txg is deferred at once: a file that spans
#	many txgs does not stop the copy for a burst of tries per txg.
#
# STRATEGY:
#	1. Create a pool with a two-way mirror and turn the async mirror on.  Write
#	   files a and wait until the lagging child has caught up.
#	2. With the copy stopped, write file b in four txgs.  Export and
#	   import, so that the ARC does not hold b.  Inject read errors on the
#	   data of b.
#	3. Restart the copy.  Wait until a block is deferred, and verify that
#	   the copy passes every txg of b within a few seconds while the
#	   blocks wait: a burst of tries per txg would take 12 s for each.
#	   Clear the injection.
#	4. Wait until no block is deferred.  Verify that a block was
#	   recovered, that no data error was counted, the event, and that the
#	   copy neither failed nor froze.
#	5. With the copy stopped, write file c, export and import, and inject
#	   read errors on the data of c that stay.  Restart the copy and wait
#	   until no block is deferred.  Verify that a data error is counted,
#	   that nothing was recovered, and that the copy goes on: write file
#	   d and wait until the lagging child has it.
#	6. Import the lagging child alone.  Verify files a, b and d, and that
#	   c cannot be read.
#

verify_runnable "global"

typeset event_file=$TEST_BASE_DIR/async_mirror_deferred_events.$$

function cleanup
{
	[[ -e $ASYNC_MIRROR_HIDDEN ]] && async_mirror_unhide_leader
	cleanup_async_mirror
	rm -f "$event_file"
}

#
# Stop the copy, write a file, and export and import the pool, so that the
# ARC does not hold the file.  The copy stays stopped.
#
# $1 path of the file, 16 blocks of 128k
# $2 number of txgs that write the file, a divisor of 16 (1)
#
function write_behind # file [txgs]
{
	typeset -i i n

	(( n = 16 / ${2:-1} ))

	async_mirror_suspend_copy 1
	async_mirror_write $1 $n
	sync_pool $TESTPOOL
	for (( i = 1; i < ${2:-1}; i++ )); do
		log_must file_write -o append -f $1 -b 131072 -c $n -d R
		sync_pool $TESTPOOL
	done
	log_must zpool export $TESTPOOL
	async_mirror_import
	log_must test "$(get_async_mirror_stat pending_txgs)" -gt 0
}

log_onexit cleanup

async_mirror_create_pool
async_mirror_enable
async_mirror_write_files a 2
log_must wait_async_mirror_idle
typeset digests="$(async_mirror_digests a 2)"

# An error that passes after the tries of the copy.
log_must async_mirror_set_tunable32 ASYNC_MIRROR_READ_RETRIES 4
write_behind /$TESTPOOL/b 4
log_must zpool events -c
log_must zinject -a -t data -e io -T read /$TESTPOOL/b

async_mirror_suspend_copy 0
typeset -i i
log_must wait_async_mirror_stat data_deferred -gt 0
#
# The copy goes on while the blocks wait for their tries: the copy of each
# later txg of b defers its blocks at once, with no burst of tries.
#
log_must wait_async_mirror_stat pending_txgs -eq 0
log_must test "$(get_async_mirror_stat data_deferred)" -gt 0
log_must zinject -c all

log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat data_recovered)" -ge 1
log_must test "$(get_async_mirror_stat data_errors)" == 0
log_must test "$(get_async_mirror_stat failed)" == 0
log_must test "$(get_async_mirror_stat frozen)" == 0
log_must eval "zpool events -v > $event_file"
# The count is printed as hex ("0x10"): add it up in shell arithmetic, not
# in awk, which would compare the text.  One try can recover only some of
# the blocks, so accept several events whose counts add up to more than one.
typeset -i events=$(grep -c 'sysevent\.fs\.zfs\.async_mirror_recovered' \
    "$event_file")
typeset -i total=0
typeset c
for c in $(awk '/async_mirror_count =/ { print $3 }' "$event_file"); do
	((total += 16#${c#0x}))
done
log_must test $events -ge 1
log_must test $total -gt 1
digests="$digests
$(xxh128digest /$TESTPOOL/b)"

# An error that stays.
log_must async_mirror_set_tunable32 ASYNC_MIRROR_READ_RETRIES 1
write_behind /$TESTPOOL/c
log_must zinject -a -t data -e io -T read /$TESTPOOL/c
async_mirror_suspend_copy 0
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat data_errors)" -ge 1
log_must test "$(get_async_mirror_stat data_recovered)" == 0
log_must test "$(get_async_mirror_stat failed)" == 0
log_must test "$(get_async_mirror_stat frozen)" == 0
log_must zinject -c all
async_mirror_write /$TESTPOOL/d 16
sync_pool $TESTPOOL
log_must wait_async_mirror_idle
log_must test "$(get_async_mirror_stat active)" == 1
digests="$digests
$(xxh128digest /$TESTPOOL/d)"
log_must zpool export $TESTPOOL

async_mirror_import_lagging
log_must test "$digests" == "$(async_mirror_digests a 2)
$(xxh128digest /$TESTPOOL/b)
$(xxh128digest /$TESTPOOL/d)"
log_mustnot eval "cat /$TESTPOOL/c > /dev/null"
log_must zpool export $TESTPOOL
async_mirror_unhide_leader

log_pass "A deferred data block is copied when its read error passes."
