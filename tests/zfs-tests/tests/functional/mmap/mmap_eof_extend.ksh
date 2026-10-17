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

#
# DESCRIPTION:
#	Data stored through a shared mapping past the end of a file must not
#	become part of the file when the file is extended (openzfs #19220).
#
# STRATEGY:
#	1. For each way of extending a file (ftruncate, a write past the end
#	   of the file, fallocate, and cloning a block past the end of the
#	   file), store data past the end of a file through a mapping of its
#	   last page, extend the file, and verify the bytes past the old end
#	   of file read back as zeros.  Do this both with the page still
#	   dirty when the file is extended and after writing it back.
#	2. Remount the file system and verify the bytes are zeros on disk.
#

verify_runnable "global"

if ! is_linux; then
	log_unsupported "fallocate and FICLONERANGE are Linux-only"
fi

FS=$TESTPOOL/$TESTFS
PAGESIZE=$(getconf PAGESIZE)
ZEROS="00000000000000000000"

function cleanup
{
	log_must rm -f $TESTDIR/eof.*
	log_must zfs inherit recordsize $FS
	if tunable_exists BCLONE_ENABLED; then
		log_must restore_tunable BCLONE_ENABLED
	fi
}

# bytes_at FILE OFFSET: the 10 bytes at OFFSET in FILE, in hex
function bytes_at
{
	dd if=$1 bs=1 skip=$2 count=10 2>/dev/null | od -An -tx1 | tr -d ' \n'
}

log_assert "Data stored through a mapping past EOF is not exposed by" \
    "extending the file"

log_onexit cleanup

if tunable_exists BCLONE_ENABLED; then
	log_must save_tunable BCLONE_ENABLED
	log_must set_tunable32 BCLONE_ENABLED 1
fi
# Cloning needs a file of several blocks
log_must zfs set recordsize=4k $FS

typeset -i failed=0
for mode in truncate write fallocate clone; do
	for sync in "" "-s"; do
		file=$TESTDIR/eof.$mode$sync
		if [[ $mode == clone ]]; then
			log_must dd if=/dev/urandom of=$file.src bs=4k count=1
			sync_pool $TESTPOOL
		fi
		out=$(mmap_eof_extend $sync $mode $file)
		ret=$?
		log_note "$out"
		if (( ret == 2 )); then
			log_fail "mmap_eof_extend $sync $mode failed"
		elif (( ret != 0 )); then
			log_note "$mode$sync: stale data past the old EOF"
			(( failed += 1 ))
		fi
	done
done

# Read the files back from disk
log_must zfs unmount $FS
log_must zfs mount $FS
for file in $TESTDIR/eof.*; do
	[[ $file == *.src ]] && continue
	if [[ $file == *clone* ]]; then
		off=$((PAGESIZE + 10))
	else
		off=10
	fi
	bytes=$(bytes_at $file $off)
	log_note "$file on disk: $bytes"
	if [[ $bytes != $ZEROS ]]; then
		log_note "$file: stale data on disk past the old EOF"
		(( failed += 1 ))
	fi
done

if (( failed > 0 )); then
	log_fail "$failed checks found data stored past EOF in the file"
fi

log_pass "Data stored through a mapping past EOF is not exposed by" \
    "extending the file"
