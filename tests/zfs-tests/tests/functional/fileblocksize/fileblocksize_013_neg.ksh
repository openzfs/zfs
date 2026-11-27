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
# Copyright (c) 2026 by Will Rouesnel. All rights reserved.
#
#
# DESCRIPTION:
#	A block size above recordsize or at or below special_small_blocks is
#	refused, and so is any change on an immutable file or directory.
#
# STRATEGY:
#	1. In a dataset with recordsize=32K and special_small_blocks=8K, check
#	   that 64K and 8K are refused on a file and a directory and 16K and
#	   32K are allowed.
#	2. Raise special_small_blocks past the directory's size and check that
#	   new files no longer get it, while the pinned file keeps its size.
#	3. With special_small_blocks equal to and above recordsize, where
#	   every block goes to the special class anyway, check that sizes up
#	   to recordsize are allowed and larger ones still refused.
#	4. Make a file and a directory immutable and check that setting and
#	   clearing fail with EPERM, then work once they are mutable again.
#

. $STF_SUITE/tests/functional/fileblocksize/fileblocksize.kshlib

verify_runnable "both"

typeset fs=$TESTPOOL/$TESTFS/limits

function set_immutable # on|off path
{
	if is_freebsd; then
		[[ $1 == on ]] && chflags schg $2 || chflags noschg $2
	else
		[[ $1 == on ]] && chattr +i $2 || chattr -i $2
	fi
}

function cleanup
{
	if [[ -n "$mnt" ]]; then
		set_immutable off $mnt/imm 2>/dev/null
		set_immutable off $mnt/immdir 2>/dev/null
	fi
	datasetexists $fs && destroy_dataset $fs -r
}

log_onexit cleanup
log_assert "block sizes outside the dataset's limits and immutable " \
    "files are refused"

log_must zfs create -o recordsize=32k -o special_small_blocks=8k $fs
typeset mnt=$(get_prop mountpoint $fs)
log_must touch $mnt/f
log_must mkdir $mnt/d

for p in $mnt/f $mnt/d; do
	extsize_fails EINVAL -s 65536 $p
	extsize_fails EINVAL -s 8192 $p
	extsize_fails EINVAL -s 4096 $p
	log_must extsize -s 32768 $p
	log_must extsize -s 16384 $p
done
verify_ext $mnt/f extsize 16384
verify_ext $mnt/d extszinherit 16384

log_must touch $mnt/d/before
verify_ext $mnt/d/before extsize 16384
log_must zfs set special_small_blocks=16k $fs
log_must touch $mnt/d/after
verify_ext $mnt/d/after none 0
verify_ext $mnt/d/before extsize 16384
write_mib $mnt/d/before
verify_blksz $mnt/d/before 16384
log_must zfs set special_small_blocks=0 $fs
log_must zfs set recordsize=8k $fs
log_must touch $mnt/d/after2
verify_ext $mnt/d/after2 none 0
log_must zfs set recordsize=32k $fs

for small in 32k 64k; do
	log_must zfs set special_small_blocks=$small $fs
	for s in 4096 16384 32768; do
		log_must touch $mnt/f$small.$s
		log_must mkdir $mnt/d$small.$s
		log_must extsize -s $s $mnt/f$small.$s
		log_must extsize -s $s $mnt/d$small.$s
		verify_ext $mnt/f$small.$s extsize $s
		verify_ext $mnt/d$small.$s extszinherit $s
	done
	log_must touch $mnt/f$small.big
	extsize_fails EINVAL -s 65536 $mnt/f$small.big
	log_must touch $mnt/d$small.4096/new
	verify_ext $mnt/d$small.4096/new extsize 4096
done
log_must zfs set special_small_blocks=0 $fs

log_must touch $mnt/imm $mnt/immset
log_must mkdir $mnt/immdir
log_must extsize -s 32768 $mnt/immset
log_must set_immutable on $mnt/imm
log_must set_immutable on $mnt/immset
log_must set_immutable on $mnt/immdir
extsize_fails EPERM -s 16384 $mnt/imm
extsize_fails EPERM -c $mnt/immset
extsize_fails EPERM -s 16384 $mnt/immdir
verify_ext $mnt/imm none 0
verify_ext $mnt/immset extsize 32768
verify_ext $mnt/immdir none 0
log_must set_immutable off $mnt/imm
log_must set_immutable off $mnt/immset
log_must set_immutable off $mnt/immdir
log_must extsize -s 16384 $mnt/imm
log_must extsize -c $mnt/immset
log_must extsize -s 16384 $mnt/immdir

log_pass "block sizes outside the dataset's limits and immutable " \
    "files are refused"
