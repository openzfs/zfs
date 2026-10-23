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

# DESCRIPTION:
#	copy_file_range(2), lseek(2) with SEEK_DATA and zfs rewrite
#	write out a file's dirty mapped pages before they start.  A
#	writer of the dataset's teardown lock, such as zfs set
#	defaultuserquota, must still finish while they do.
#
# STRATEGY:
#	1. Keep every other page of a file dirty through a shared
#	   mapping, so each write-out has many separate pages.
#	2. For each of copy_file_range, SEEK_DATA and zfs rewrite,
#	   run it on the file in a loop, and check that the dataset's
#	   ZIL commits rise, which shows mapped pages being written
#	   out.
#	3. Meanwhile set defaultuserquota again and again for 10
#	   seconds, require each set to finish within 60 seconds,
#	   and check that the ZIL commits kept rising.
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "global"

typeset -r FS=$TESTPOOL/$TESTFS
typeset -r FILE=$TESTDIR/file
typeset -r DST=$TESTDIR/dst
typeset -r WORK=$TEST_BASE_DIR/mmap_flush_teardown
typeset -r SEEK="import os, sys
os.lseek(os.open(sys.argv[1], os.O_RDONLY), 0, os.SEEK_DATA)"
typeset dirty_pid=""
typeset loop_pid=""
typeset -i hung=0

function cleanup
{
	tunable_exists BCLONE_ENABLED &&
	    log_must restore_tunable BCLONE_ENABLED
	touch $WORK/stop
	((hung)) && return
	[[ -n $loop_pid ]] && wait $loop_pid
	[[ -n $dirty_pid ]] && wait $dirty_pid
	log_must zfs set defaultuserquota=none $FS
	rm -rf $WORK $FILE $DST
}

#
# Run one write-out path on the file until $WORK/stop exists.
#
function writeout_loop # copy | seek | rewrite
{
	while [[ ! -f $WORK/stop ]]; do
		case $1 in
		copy)
			clonefile -f -q $FILE $DST ;;
		seek)
			python3 -c "$SEEK" $FILE ;;
		rewrite)
			zfs rewrite -o 0 -l 131072 $FILE ;;
		esac || echo $1 >> $WORK/errors
	done
}

function check_path # copy | seek | rewrite
{
	typeset -i c0 c1 c2 i t start

	rm -f $WORK/stop
	python3 $WORK/dirty.py $FILE $WORK/stop &
	dirty_pid=$!
	writeout_loop $1 &
	loop_pid=$!

	c0=$(kstat_dataset $FS zil_commit_count)
	log_must sleep 5
	c1=$(kstat_dataset $FS zil_commit_count)
	log_note "$1: $((c1 - c0)) ZIL commits before the sets"
	log_must [ $c1 -gt $c0 ]

	start=$SECONDS
	for ((i = 0; SECONDS - start < 10; i++)); do
		rm -f $WORK/rc
		(
			zfs set defaultuserquota=$((i % 2 + 1))G $FS
			echo $? > $WORK/rc
		) &
		for ((t = 0; t < 600; t++)); do
			[[ -s $WORK/rc ]] && break
			sleep 0.1
		done
		if [[ ! -s $WORK/rc ]]; then
			hung=1
			log_fail "$1: zfs set did not finish in 60s"
		fi
		wait $!
		log_must [ "$(<$WORK/rc)" = 0 ]
	done
	c2=$(kstat_dataset $FS zil_commit_count)
	log_note "$1: $i sets, $((c2 - c1)) ZIL commits during them"
	log_must [ $c2 -gt $c1 ]

	touch $WORK/stop
	wait $loop_pid
	wait $dirty_pid
	loop_pid=""
	dirty_pid=""
}

log_assert "Teardown writers finish while mapped pages flush"
log_onexit cleanup

if tunable_exists BCLONE_ENABLED; then
	log_must save_tunable BCLONE_ENABLED
	log_must set_tunable32 BCLONE_ENABLED 1
fi

log_must rm -rf $WORK
log_must mkdir $WORK
log_must file_write -o create -f $FILE -b 1048576 -c 16 -d R
log_must sync_pool $TESTPOOL

cat > $WORK/dirty.py <<'EOF'
import mmap
import os
import sys

path, stop = sys.argv[1], sys.argv[2]
fd = os.open(path, os.O_RDWR)
size = os.fstat(fd).st_size
m = mmap.mmap(fd, size)
while not os.path.exists(stop):
    for off in range(0, size, 2 * mmap.PAGESIZE):
        m[off] = (m[off] + 1) & 0xff
m.close()
os.close(fd)
EOF

for p in copy seek rewrite; do
	check_path $p
done

if [[ -s $WORK/errors ]]; then
	log_note "failed operations: $(sort $WORK/errors | uniq -c)"
fi
log_must [ ! -s $WORK/errors ]

log_pass "Teardown writers finish while mapped pages flush"
