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
# Copyright (c) 2007, Sun Microsystems Inc. All rights reserved.
# Copyright (c) 2013, 2016, Delphix. All rights reserved.
# Copyright (c) 2019, Kjeld Schouten-Lebbing. All Rights Reserved.
# Use is subject to license terms.
#

. $STF_SUITE/include/properties.shlib
. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# With 'compression' set, a file with non-power-of-2 blocksize storage space
# can be freed as will normally.
#
# STRATEGY:
#	1. Set 'compression' or 'compress' to on or lzjb
#	2. Set different recordsize with ZFS filesystem
#	3. Repeatedly using 'randfree_file' to create a file and then free its
#	   storage space with different range, the system should work normally.
#

verify_runnable "both"

function cleanup
{
	rm -f $TESTDIR/*
}

function create_free_testing #<file size> <file>
{
	typeset -i fsz=$1
	typeset file=$2
	typeset -i start=0
	typeset -i len=0
	typeset -i dist=0

	for start in 0 $((RANDOM % fsz))
	do
		(( dist = fsz - start ))
		for len in $((1 + RANDOM % (dist - 1))) $dist \
		    $((start + dist)); do
			log_must randfree_file -l $fsz -s $start -n $len $file
			[[ -e $file ]] && \
				log_must rm -f $file
		done
	done
}


log_assert "Creating non-power-of-2 blocksize file and freeing the file \
	storage space at will should work normally with compression setting"
log_onexit cleanup

fs=$TESTPOOL/$TESTFS
single_blk_file=$TESTDIR/singleblkfile.$$
multi_blk_file=$TESTDIR/multiblkfile.$$
typeset -i blksize=512
typeset -i fsize=0
typeset -i avail=0
typeset -i blknum=0

for propname in "compression" "compress"
do
	for value in "${compress_prop_vals[@]:1}"
	do
		log_must zfs set compression=$value $fs
		real_val=$(get_prop $propname $fs)
		if [[ $value == "gzip-6" ]]; then
			value="gzip"
		fi
		[[ $real_val != $value ]] && \
			log_fail "Set property $propname=$value failed."

		(( blksize = 512 ))
		while (( blksize <= 131072 )); do
			log_must zfs set recordsize=$blksize $fs

			# doing single block testing
			(( fsize = $RANDOM ))
			if (( fsize > blksize )); then
				(( fsize = fsize % blksize ))
			fi
			if (( (fsize % 2) == 0 )); then
				#make sure fsize is non-power-of-2
				(( fsize = fsize + 1 ))
			fi
			create_free_testing $fsize $single_blk_file

			# doing multiple blocks testing
			avail=$(get_prop available $fs)
			(( blknum = avail / blksize ))
			# we just test <10 multi-blocks to limit testing time
			(( blknum = blknum % 9 ))
			while (( blknum < 2 )); do
				(( blknum = blknum + $RANDOM % 9 ))
			done
			if (( (blknum % 2) == 0 )); then
				(( blknum = blknum + 1 )) # keep blknum as odd
			fi
			(( fsize = blknum * blksize ))
			create_free_testing $fsize $multi_blk_file

			(( blksize = blksize * 2 ))
		done
	done
done

log_pass "Creating and freeing non-power-of-2 blocksize file work as expected."
