#!/bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2020 The FreeBSD Foundation [1]
#
# [1] Portions of this software were developed by Allan Jude
#     under sponsorship from the FreeBSD Foundation.

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# ZFS should receive a ZSTD compressed block and be able to determine the level
#
# STRATEGY:
# 1. Create a ZSTD compressed dataset (random level)
# 2. Create and checksum a file on the compressed dataset
# 3. Snapshot the compressed dataset
# 4. Attempt to receive the snapshot into a new dataset
# 5. Verify the checksum of the file is the same as the original
# 6. Verify the compression level is correctly stored
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1 -r

	datasetexists $TESTPOOL/$TESTFS2 && \
		destroy_dataset $TESTPOOL/$TESTFS2 -r
}

log_onexit cleanup

log_assert "ZFS should track compression level when receiving a ZSTD stream"

typeset src_data="$STF_SUITE/tests/functional/cli_root/zfs_receive/zstd_test_data.txt"
typeset snap="$TESTPOOL/$TESTFS1@snap"

random_level=$((RANDOM%19 + 1))
log_note "Randomly selected ZSTD level: $random_level"

log_must zfs create -o compress=zstd-$random_level $TESTPOOL/$TESTFS1
# Make a 5kb compressible file
log_must cat $src_data $src_data $src_data $src_data $src_data \
    > /$TESTPOOL/$TESTFS1/$TESTFILE0
typeset checksum=$(md5digest /$TESTPOOL/$TESTFS1/$TESTFILE0)

log_must zfs snapshot $snap

# get object number of file
listing=$(ls -i /$TESTPOOL/$TESTFS1/$TESTFILE0)
set -A array $listing
obj=${array[0]}
log_note "file /$TESTPOOL/$TESTFS1/$TESTFILE0 has object number $obj"

output=$(zdb -Zddddddbbbbbb $TESTPOOL/$TESTFS1 $obj 2> /dev/null \
    |grep -m 1 "L0 DVA" |head -n1)
dva=$(sed -Ene 's/^.+DVA\[0\]=<([^>]+)>.*$/\1/p' <<< "$output")
log_note "block 0 of /$TESTPOOL/$TESTFS1/$TESTFILE0 has a DVA of $dva"

zstd_str=$(sed -Ene 's/^.+ ZSTD:size=([^:]+):version=([^:]+):level=([^:]+):.*$/\1:\2:\3/p' <<< "$output")
zstd_size1=$(echo "$zstd_str" |awk '{split($0,array,":")} END{print array[1]}')
zstd_version1=$(echo "$zstd_str" |awk '{split($0,array,":")} END{print array[2]}')
zstd_level1=$(echo "$zstd_str" |awk '{split($0,array,":")} END{print array[3]}')
log_note "ZSTD src: size=$zstd_size1 version=$zstd_version1 level=$zstd_level1"

log_note "Verify ZFS can receive the ZSTD compressed stream"
log_must eval "zfs send -ec $snap | zfs receive $TESTPOOL/$TESTFS2"

typeset cksum1=$(md5digest /$TESTPOOL/$TESTFS2/$TESTFILE0)
[[ "$cksum1" == "$checksum" ]] || \
	log_fail "Checksums differ ($cksum1 != $checksum)"

# get object number of file
listing=$(ls -i /$TESTPOOL/$TESTFS2/$TESTFILE0)
set -A array $listing
obj=${array[0]}
log_note "file /$TESTPOOL/$TESTFS2/$TESTFILE0 has object number $obj"

output=$(zdb -Zddddddbbbbbb $TESTPOOL/$TESTFS2 $obj 2> /dev/null \
    |grep -m 1 "L0 DVA" |head -n1)
dva=$(sed -Ene 's/^.+DVA\[0\]=<([^>]+)>.*$/\1/p' <<< "$output")
log_note "block 0 of /$TESTPOOL/$TESTFS2/$TESTFILE0 has a DVA of $dva"

zstd_str=$(sed -Ene 's/^.+ ZSTD:size=([^:]+):version=([^:]+):level=([^:]+):.*$/\1:\2:\3/p' <<< "$output")
zstd_size2=$(echo "$zstd_str" |awk '{split($0,array,":")} END{print array[1]}')
(( $zstd_size2 != $zstd_size1 )) && log_fail \
"ZFS recv failed: compressed size differs ($zstd_size2 != $zstd_size1)"
zstd_version2=$(echo "$zstd_str" |awk '{split($0,array,":")} END{print array[2]}')
zstd_level2=$(echo "$zstd_str" |awk '{split($0,array,":")} END{print array[3]}')
log_note "ZSTD dest: size=$zstd_size2 version=$zstd_version2 level=$zstd_level2"
(( $zstd_level2 != $zstd_level1 )) && log_fail \
"ZFS recv failed: compression level did not match header level ($zstd_level2 != $zstd_level1)"

log_pass "ZFS can receive a ZSTD stream and determine the compression level"
