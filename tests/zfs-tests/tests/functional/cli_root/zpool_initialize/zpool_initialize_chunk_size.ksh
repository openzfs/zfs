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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_initialize/zpool_initialize.kshlib

#
# DESCRIPTION:
# Initializing bounds, aligns, and snapshots the initialize chunk size.
#
# STRATEGY:
# 1. Start initializing with zero, a sub-sector value, a greater-than-16 MiB
#    value, and sector-unaligned values below and above the sector size.
#    Verify that initializing reports no errors and writes whole sectors.
# 2. Start with a sector-sized chunk, change the tunable while initializing,
#    and suspend the operation.
# 3. Change the tunable while suspended, resume, change it again while active,
#    and verify the same.
#

DISK1="$(echo $DISKS | cut -d' ' -f1)"

function cleanup
{
	zinject -c all
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	restore_tunable INITIALIZE_CHUNK_SIZE
}
log_onexit cleanup

log_must save_tunable INITIALIZE_CHUNK_SIZE

max_chunk_sz=$((16 * 1024 * 1024))
sector_sz=4096

#
# Initializing must report no write errors and must have written only whole
# sectors.
#
function verify_initialize_healthy
{
	typeset errors bytes

	errors=$(initialize_stat $TESTPOOL $DISK1 init_errors) || \
	    log_fail "Could not read initializing errors"
	bytes=$(initialize_stat $TESTPOOL $DISK1 initialized) || \
	    log_fail "Could not read initialized bytes"
	(( errors == 0 )) || log_fail "Initializing reported $errors errors"
	(( bytes % sector_sz == 0 )) || \
	    log_fail "Initialized $bytes bytes, not a multiple of $sector_sz"
}

function start_initialize
{
	log_must zpool initialize $TESTPOOL $DISK1
	[[ -z "$(initialize_progress $TESTPOOL $DISK1)" ]] && \
	    log_fail "Initializing did not start"
}

#
# Change the tunable while initializing is active. Writes are delayed by
# 100 ms, so waiting a second before the change lets the thread sample the
# tunable first, and waiting a second after it gives the thread time to issue
# more writes.
#
function change_while_active # chunk_size
{
	sleep 1
	log_must set_tunable64 INITIALIZE_CHUNK_SIZE $1
	sleep 1
	verify_initialize_healthy
}

function test_chunk_size # chunk_size
{
	log_must set_tunable64 INITIALIZE_CHUNK_SIZE $1
	log_must zpool create -f -m none -o ashift=12 $TESTPOOL $DISK1
	start_initialize
	sleep 1
	verify_initialize_healthy
	log_must zpool destroy -f $TESTPOOL
}

for chunk_size in 0 1 $((sector_sz - 1)) $((sector_sz + 1)) \
    $((max_chunk_sz + 1)); do
	test_chunk_size $chunk_size
done

log_must set_tunable64 INITIALIZE_CHUNK_SIZE $sector_sz
log_must zpool create -f -m none -o ashift=12 $TESTPOOL $DISK1
log_must zinject -d $DISK1 -D 100:1 -T write $TESTPOOL
start_initialize

# Changing the tunable must not invalidate the active thread's allocation and
# write size.
change_while_active $((max_chunk_sz + 1))
log_must zpool initialize -s $TESTPOOL $DISK1
log_must eval "initialize_prog_line $TESTPOOL $DISK1 | grep suspended"

# Resuming starts a new thread, which samples the tunable again and writes
# 8 KiB chunks from where the suspended thread stopped. Changing the tunable
# while that thread is active must not invalidate its allocation or write size.
log_must set_tunable64 INITIALIZE_CHUNK_SIZE $((2 * sector_sz + 1))
start_initialize
log_mustnot eval "initialize_prog_line $TESTPOOL $DISK1 | grep suspended"
change_while_active 0

log_pass "Initializing bounds, aligns, and snapshots its chunk size"
