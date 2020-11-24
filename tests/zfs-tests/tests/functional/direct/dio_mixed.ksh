#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
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
	check_dio_write_chksum_verify_failures $TESTPOOL "raidz" 0
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
		log_must cmp_md5s $new_file $tmp_file
		log_must rm -f $new_file $tmp_file

		# Verify direct write followed by a buffered read.
		log_must stride_dd -i $src_file -o $new_file -b $obs \
		    -c $oblocks $oflags
		log_must stride_dd -i $new_file -o $tmp_file -b $ibs \
		    -c $iblocks
		log_must cmp_md5s $new_file $tmp_file
		log_must rm -f $new_file $tmp_file

		# Verify direct write followed by a direct read.
		log_must stride_dd -i $src_file -o $new_file -b $obs \
		    -c $oblocks $oflags
		log_must stride_dd -i $new_file -o $tmp_file -b $ibs \
		    -c $iblocks $iflags
		log_must cmp_md5s $new_file $tmp_file
		log_must rm -f $new_file $tmp_file
	done
done

log_pass "Verify mixed buffered and Direct I/O are coherent."
