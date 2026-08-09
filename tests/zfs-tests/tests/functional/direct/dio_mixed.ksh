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
# Copyright (c) 2021 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/properties.shlib
. $STF_SUITE/tests/functional/direct/dio.cfg
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
# 	Verify mixed buffered and Direct I/O are coherent.
#
# STRATEGY:
#	1. Verify interleaved buffered and Direct I/O
#

verify_runnable "global"

function cleanup
{
	log_must rm -f $src_file $new_file $tmp_file
}

log_assert "Verify mixed buffered and Direct I/O are coherent."

log_onexit cleanup

mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS)

src_file=$mntpnt/src_file
new_file=$mntpnt/new_file
tmp_file=$mntpnt/tmp_file
page_size=$(getconf PAGESIZE)
file_size=1048576

log_must stride_dd -i /dev/urandom -o $src_file -b $file_size -c 1

#
# Using mixed input and output block sizes verify that buffered and
# Direct I/O can be interleaved and the result with always be coherent.
#
for ibs in "512" "$page_size" "131072"; do
	for obs in "512" "$page_size" "131072"; do
		iblocks=$(($file_size / $ibs))
		oblocks=$(($file_size / $obs))
		iflags=""
		oflags=""

		# Only allow Direct I/O when it is at least page sized.
		if [[ $ibs -ge $page_size ]]; then
			iflags="-d"
		fi

		if [[ $obs -ge $page_size ]]; then
			oflags="-D"
		fi

		# Verify buffered write followed by a direct read.
		log_must stride_dd -i $src_file -o $new_file -b $obs \
		    -c $oblocks
		log_must stride_dd -i $new_file -o $tmp_file -b $ibs \
		    -c $iblocks $iflags
		log_must cmp_xxh128 $new_file $tmp_file
		log_must rm -f $new_file $tmp_file

		# Verify direct write followed by a buffered read.
		log_must stride_dd -i $src_file -o $new_file -b $obs \
		    -c $oblocks $oflags
		log_must stride_dd -i $new_file -o $tmp_file -b $ibs \
		    -c $iblocks
		log_must cmp_xxh128 $new_file $tmp_file
		log_must rm -f $new_file $tmp_file

		# Verify direct write followed by a direct read.
		log_must stride_dd -i $src_file -o $new_file -b $obs \
		    -c $oblocks $oflags
		log_must stride_dd -i $new_file -o $tmp_file -b $ibs \
		    -c $iblocks $iflags
		log_must cmp_xxh128 $new_file $tmp_file
		log_must rm -f $new_file $tmp_file
	done
done

log_pass "Verify mixed buffered and Direct I/O are coherent."
