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
#	A store through a shared mapping, into a block the apply has
#	already locked, neither hangs the apply nor gets lost.  One
#	batch redirects blocks 0 and 1 of one file, and both blocks
#	have pages in the page cache.  The test dirties block 0
#	through the mapping after the batch locks it and before the
#	batch reaches block 1.  Writing that page back needs the range
#	lock of block 0.  The run must finish, and once the page cache
#	is dropped the file must hold the store.  The batch clones
#	both blocks, and the run ends at least a quarter delay after
#	the store.
#

verify_runnable "global"

# ms the apply waits before each block; the store lands in the wait
typeset -r CD_MM_DELAY=4000
typeset -r CD_MM_OFF=8192
typeset -r CD_MM_MARK="clonedup mmap store"
typeset -r CD_MM_PY=$TEST_BASE_DIR/clonedup_mm.py
typeset -r CD_MM_READY=$TEST_BASE_DIR/clonedup_mm_ready
typeset -r CD_MM_GO=$TEST_BASE_DIR/clonedup_mm_go
typeset -r CD_MM_DONE=$TEST_BASE_DIR/clonedup_mm_done
typeset -r CD_MM_WANT=$TEST_BASE_DIR/clonedup_mm_want
typeset hung=""
typeset helper_pid=""

function cleanup
{
	[[ -n $helper_pid ]] && kill $helper_pid 2>/dev/null
	log_must restore_tunable CLONEDUP_APPLY_TXG_DELAY
	log_must restore_tunable CLONEDUP_APPLY_THREADS
	rm -f $CD_MM_PY $CD_MM_READY $CD_MM_GO $CD_MM_DONE $CD_MM_WANT
	if [[ -n $hung ]]; then
		log_note "$hung; $TESTPOOL is left in place"
		return 0
	fi
	clonedup_cleanup
}

# Wait up to $2 seconds for file $1 to appear.
function clonedup_mm_file # path seconds
{
	typeset -i i

	for ((i = 0; i < $2 * 10; i++)); do
		[[ -f $1 ]] && return 0
		sleep 0.1
	done
	return 1
}

# Wait up to $3 seconds for kstat counter $1 to reach $2.
function clonedup_mm_kstat # name value seconds
{
	typeset -i i v

	for ((i = 0; i < $3 * 10; i++)); do
		v=$(clonedup_kstat $1)
		(( v >= $2 )) && return 0
		sleep 0.1
	done
	return 1
}

# Where the apply thread is stuck, for the log.
function clonedup_mm_stacks
{
	typeset p

	is_linux || return 0
	for p in $(pgrep z_clonedup); do
		log_note "stack of $p" \
		    "($(cat /proc/$p/comm 2>/dev/null)):" \
		    "$(cat /proc/$p/stack 2>/dev/null)"
	done
}

log_assert "a store through a mapping during the apply is not lost"
log_onexit cleanup
log_must save_tunable CLONEDUP_APPLY_TXG_DELAY
log_must save_tunable CLONEDUP_APPLY_THREADS

clonedup_pool_create
log_must zfs create $TESTPOOL/mm
typeset dir=/$TESTPOOL/mm

# s is the older copy, so f is the destination of both blocks.
clonedup_write $dir/s 2
clonedup_sync
clonedup_dup $dir/s $dir/f
clonedup_sync
log_must dd if=$dir/f of=$CD_MM_WANT bs=$CD_BS status=none
printf "%s" "$CD_MM_MARK" | dd of=$CD_MM_WANT bs=1 seek=$CD_MM_OFF \
    conv=notrunc status=none

log_must set_tunable32 CLONEDUP_APPLY_THREADS 1
log_must set_tunable32 CLONEDUP_APPLY_TXG_DELAY $CD_MM_DELAY

#
# Map the file and fault in a page of each block, so the store later
# finds its page cached and needs no range lock to land.  Then wait
# for the go file, store without msync, and leave the page dirty.
#
cat > $CD_MM_PY <<'EOF'
import mmap
import os
import sys
import time

path, ready, go, done = sys.argv[1:5]
bs, off = int(sys.argv[5]), int(sys.argv[6])
mark = sys.argv[7].encode()

fd = os.open(path, os.O_RDWR)
m = mmap.mmap(fd, 2 * bs)
touched = m[off] + m[bs + off]
open(ready, "w").close()
deadline = time.time() + 300
while not os.path.exists(go):
    if time.time() > deadline:
        sys.exit(2)
    time.sleep(0.05)
m[off:off + len(mark)] = mark
open(done, "w").close()
m.close()
os.close(fd)
EOF

rm -f $CD_MM_READY $CD_MM_GO $CD_MM_DONE
python3 $CD_MM_PY $dir/f $CD_MM_READY $CD_MM_GO $CD_MM_DONE $CD_BS \
    $CD_MM_OFF "$CD_MM_MARK" &
helper_pid=$!
clonedup_mm_file $CD_MM_READY 60 ||
    log_fail "the mapping helper never started"

#
# The run opens f just before the wait that precedes block 0.  The
# batch locks block 0 one delay later and reaches block 1 two delays
# later, so the store lands halfway between.
#
typeset -i v0=$(clonedup_kstat dst_mounted)
log_must zpool clonedup $TESTPOOL
clonedup_mm_kstat dst_mounted $((v0 + 1)) 120 ||
    log_fail "the run did not reach $dir/f"
log_must sleep $((CD_MM_DELAY * 3 / 2000))
log_must touch $CD_MM_GO
clonedup_mm_file $CD_MM_DONE 30 ||
    log_fail "the store did not complete"
typeset -F stored=$SECONDS

timeout 120 zpool wait -t clonedup $TESTPOOL
typeset -i rc=$?
typeset -F took=$((SECONDS - stored))
if (( rc != 0 )); then
	hung="the run did not finish"
	clonedup_mm_stacks
	log_fail "$hung (zpool wait returned $rc)"
fi
wait $helper_pid
rc=$?
helper_pid=""
log_must [ $rc -eq 0 ]
clonedup_jstat_is errors 0
# The store landed after the batch locked block 0, so the batch
# cloned both blocks and the writeback rewrote block 0 after it.
# The batch reaches block 1 half a delay after the store.
clonedup_jstat_is clonedup_applied 2
clonedup_jstat_is clonedup_skipped_dirty 0
log_note "the run ended $took s after the store"
(( took >= CD_MM_DELAY / 4000 )) ||
    log_fail "the run ended $took s after the store, before the" \
    "batch could have reached block 1"

# Write the page back and drop it, so the compare reads the disk.
log_must sync
log_must zfs umount $TESTPOOL/mm
log_must zfs mount $TESTPOOL/mm
log_must cmp $dir/f $CD_MM_WANT

# The apply redirected block 1, and the store rewrote block 0.
clonedup_sync
clonedup_check_shared $TESTPOOL/mm s $TESTPOOL/mm f "1"
clonedup_leakcheck

log_pass "a store through a mapping during the apply is not lost"
