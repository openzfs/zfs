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
# Copyright (c) 2026, Patrick Domack. All rights reserved.
#

. $STF_SUITE/tests/functional/clonedup/clonedup.kshlib

#
# DESCRIPTION:
#	zfs set defaultuserquota takes a filesystem's teardown lock as
#	writer.  It must finish while the apply holds a destination
#	whose pages a mapping has dirtied.  On FreeBSD, writing those
#	pages out enters the filesystem again, and that entry waits
#	behind a waiting writer even in a thread that has entered
#	already.  An apply that wrote them out under its own entry
#	never got past it, and the writer waited for that entry.
#	FreeBSD wrote out every dirty page of the file, so a dirty
#	page in a block the apply does not clone is enough.  Dirty
#	pages in the blocks it clones do not work.  Once written
#	back, those blocks are dirty or rewritten, and the apply
#	skips them before its per-block delay.  Linux lets the
#	second entry through, so there the test passes either way.
#
# STRATEGY:
#	1. Write a file in one mounted filesystem, and a copy of it
#	   with one more block of its own at the end.
#	2. Map that last block and keep storing each of its pages' own
#	   bytes back, without msync, so the copy has dirty pages for
#	   the whole run.
#	3. Apply on one worker, two blocks to a batch and a second
#	   before each block.
#	4. Each time a batch opens the copy, set defaultuserquota
#	   while that batch holds it.  Require the set to return
#	   within a minute, and only after the batch has committed.
#	5. Check that the run was still applying after the last set,
#	   let it finish, and check that it cloned every block,
#	   counted no errors, and left the copy holding the file's
#	   bytes.
#

verify_runnable "global"

typeset -ri nblk=24
typeset -ri nsets=8
typeset -r CD_TM_PY=$TEST_BASE_DIR/clonedup_tm.py
typeset -r CD_TM_READY=$TEST_BASE_DIR/clonedup_tm.ready
typeset -r CD_TM_STOP=$TEST_BASE_DIR/clonedup_tm.stop
typeset tm_pid="" opened0 batches0
typeset -i seen=0

function cleanup
{
	[[ -n $tm_pid ]] && kill -9 $tm_pid 2>/dev/null
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
	log_must restore_tunable CLONEDUP_APPLY_THREADS
	log_must restore_tunable TXG_TIMEOUT
	rm -f $CD_TM_PY $CD_TM_READY $CD_TM_STOP
	clonedup_cleanup
}

#
# Wait for a batch to open b, then set defaultuserquota while it
# holds b.  A batch opens b once and commits two blocks, so b opened
# once more than the batches committed means a batch holds it.  The
# set takes the teardown lock as writer, so it returns only after
# that batch commits and lets go of b.
#
function cd_tm_set # quota
{
	typeset -i i
	typeset opened batches after

	for ((i = 0; i < 600; i++)); do
		opened=$(clonedup_kstat dst_mounted)
		batches=$(clonedup_kstat batches)
		((opened > seen)) && break
		sleep 0.1
	done
	clonedup_assert_counts "$opened" "$batches"
	((opened > seen)) ||
	    log_fail "no batch opened b within a minute"
	seen=$opened
	log_note "b opened $((opened - opened0)) times," \
	    "$((batches - batches0)) batches committed"
	((opened - opened0 == batches - batches0 + 1)) ||
	    log_fail "no batch holds b"
	kill -0 $tm_pid 2>/dev/null ||
	    log_fail "the mapping of b ended during the run"
	clonedup_bounded 60 zfs set defaultuserquota=$1 $TESTPOOL/fs
	after=$(clonedup_kstat batches)
	clonedup_assert_counts "$after"
	((after > batches)) ||
	    log_fail "the set returned before the batch holding b" \
	    "committed"
}

log_assert "a teardown writer finishes while a batch holds a" \
    "destination with dirty mapped pages"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_BLOCKS_PER_TXG
log_must save_tunable CLONEDUP_APPLY_THREADS
log_must save_tunable TXG_TIMEOUT

clonedup_pool_create
log_must set_tunable32 CLONEDUP_APPLY_THREADS 1
log_must set_tunable32 CLONEDUP_APPLY_BLOCKS_PER_TXG 2
log_must set_tunable32 TXG_TIMEOUT 1

# a is the older copy, so b is the destination of every block.
log_must zfs create $TESTPOOL/fs
clonedup_write /$TESTPOOL/fs/a $nblk
clonedup_sync
clonedup_dup /$TESTPOOL/fs/a /$TESTPOOL/fs/b
log_must dd if=/dev/urandom of=/$TESTPOOL/fs/b bs=$CD_BS \
    seek=$nblk count=1 conv=notrunc status=none
clonedup_sync

cat > $CD_TM_PY <<'EOF'
import mmap
import os
import sys
import time

path, off, size, ready, stop = sys.argv[1:6]
fd = os.open(path, os.O_RDWR)
m = mmap.mmap(fd, int(size), offset=int(off))
while True:
    for p in range(0, int(size), mmap.PAGESIZE):
        m[p] = m[p]
    if not os.path.exists(ready):
        open(ready, "w").close()
    if os.path.exists(stop):
        break
    time.sleep(0.05)
m.close()
os.close(fd)
EOF
rm -f $CD_TM_READY $CD_TM_STOP
python3 $CD_TM_PY /$TESTPOOL/fs/b $((nblk * CD_BS)) $CD_BS \
    $CD_TM_READY $CD_TM_STOP &
tm_pid=$!

typeset -i i batches

for ((i = 0; i < 600; i++)); do
	[[ -e $CD_TM_READY ]] && break
	kill -0 $tm_pid 2>/dev/null ||
	    log_fail "the mapping of b ended before it was ready"
	sleep 0.1
done
[[ -e $CD_TM_READY ]] || log_fail "b was not mapped within a minute"

opened0=$(clonedup_kstat dst_mounted)
batches0=$(clonedup_kstat batches)
clonedup_assert_counts "$opened0" "$batches0"
seen=$opened0

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 1000
log_must zpool clonedup $TESTPOOL
clonedup_wait_applying
for ((i = 0; i < nsets; i++)); do
	cd_tm_set $((i % 2 + 1))G
done
zpool status $TESTPOOL | grep -q "applying" ||
    log_fail "the apply ended before the last set"
((batches = $(clonedup_kstat batches) - batches0))
log_note "$batches batches"
log_must [ $batches -ge $nsets ]

log_must touch $CD_TM_STOP
for ((i = 0; i < 600; i++)); do
	kill -0 $tm_pid 2>/dev/null || break
	sleep 0.1
done
kill -0 $tm_pid 2>/dev/null &&
    log_fail "the mapping of b did not end within a minute"
wait $tm_pid || log_fail "the mapping of b failed"
tm_pid=""

log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY 0
log_must zpool wait -t clonedup $TESTPOOL
clonedup_jstat_is errors 0
clonedup_jstat_is clonedup_applied $nblk
log_must eval "dd if=/$TESTPOOL/fs/b bs=$CD_BS count=$nblk" \
    "status=none | cmp - /$TESTPOOL/fs/a"
clonedup_leakcheck

log_pass "a teardown writer finishes while a batch holds a" \
    "destination with dirty mapped pages"
