#!/bin/ksh
# SPDX-License-Identifier: CDDL-1.0

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

#
# Copyright (c) 2020 The FreeBSD Foundation [1]
#
# [1] Portions of this software were developed by Allan Jude
#     under sponsorship from the FreeBSD Foundation.

. $STF_SUITE/include/libtest.shlib

#
# Description:
# zdb -Z pool <objid> will display the ZSTD compression header
#     This will contain the actual length of the compressed data, as well as
#     the version of ZSTD used to compress the block, and the compression level
#
# Strategy:
# 1. Create a pool, set compression to zstd-<random level>
# 2. Write some identifiable data to a file
# 3. Run zdb -Zddddddbbbbbb against the file
# 4. Record the DVA, lsize, and psize, and ZSTD header of L0 block 0
# 5. Check that the ZSTD length is less than psize
# 6. Check that the ZSTD level matches the level we requested
# 7. Run zdb -R with :dr flags and confirm the size and content match
#

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "Verify zdb -Z (read ZSTD header) works as expected"
log_onexit cleanup
src_data="$STF_SUITE/tests/functional/cli_root/zfs_receive/zstd_test_data.txt"
init_data=$TESTDIR/file1
write_count=128
blksize=131072
verify_runnable "global"
verify_disk_count "$DISKS" 2
random_level=$((RANDOM%19 + 1))

default_mirror_setup_noexit $DISKS
log_must zfs set recordsize=$blksize $TESTPOOL/$TESTFS
log_must zfs set compression=zstd-$random_level $TESTPOOL/$TESTFS

# write the 1k of text 128 times
for i in {1..$write_count}
do
	cat $src_data >> $init_data
done

sync_pool $TESTPOOL true

# get object number of file
read -r obj _ < <(ls -i $init_data)
log_note "file $init_data has object number $obj"

output=$(zdb -Zddddddbbbbbb $TESTPOOL/$TESTFS $obj 2> /dev/null \
    | grep -m 1 "L0 DVA")
dva=$(sed -Ene 's/^.+DVA\[0\]=<([^>]+)>.*$/\1/p' <<< "$output")
log_note "block 0 of $init_data has a DVA of $dva"

# use the length reported by zdb -ddddddbbbbbb
size_str=$(sed -Ene 's/^.+ size=([^ ]+) .*$/\1/p' <<< "$output")
# convert sizes to decimal
IFS='/' read -r lsize psize _ <<<"$size_str"
lsize_orig=$lsize
psize_orig=$psize
lsize=${lsize%?}
psize=${psize%?}
lsize_bytes=$((16#$lsize))
psize_bytes=$((16#$psize))
log_note "block size $size_str"

# Get the ZSTD header reported by zdb -Z
read -r zstd_size zstd_version zstd_level < <(sed -Ene 's/^.+ ZSTD:size=([^:]+):version=([^:]+):level=([^:]+):.*$/\1 \2 \3/p' <<<"$output")
log_note "ZSTD compressed size $zstd_size"
(( $psize_bytes < $zstd_size )) && log_fail \
"zdb -Z failed: physical block size was less than header content length ($psize_bytes < $zstd_size)"

log_note "ZSTD version $zstd_version"

log_note "ZSTD level $zstd_level"
(( $zstd_level != $random_level )) && log_fail \
"zdb -Z failed: compression level did not match header level ($zstd_level < $random_level)"

IFS=':' read -r vdev offset _ <<<"$dva"
# Check the first 1024 bytes
output=$(ZDB_NO_ZLE="true" zdb -R $TESTPOOL $vdev:$offset:$size_str:dr 2> /dev/null)
(( ${#output} + 1 != $blksize )) && log_fail \
"zdb -Z failed to decompress the data to the expected length (${#output} != $lsize_bytes)"
cmp $init_data - <<< "$output" ||
	log_fail "zdb -R :dr failed to decompress the data properly"

log_pass "zdb -Z flag (ZSTD compression header) works as expected"
