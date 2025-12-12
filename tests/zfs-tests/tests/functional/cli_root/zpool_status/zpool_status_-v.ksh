#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
# Copyright (c) 2025 Lawrence Livermore National Security, LLC.

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify error log ranges from 'zpool status -vv' after corrupting a file
#
# STRATEGY:
# 1. Create files with different record sizes
# 2. Create a zvol
# 3. zinject errors into files, zvol, and MOS.  Inject at different byte ranges.
# 4. Verify error reporting in zpool status with different flags

verify_runnable "both"

log_assert "Verify zpool status prints error log ranges"

# Given a list of lines in $1, look for each line somewhere in stdin (the
# zpool status output).
function check_status
{
	# Read stdin
	out="$(cat -)"

	lines="$1"
	while IFS= read -r line; do
		if ! echo "$out" | grep -Fq "$line" ; then
			log_fail "Didn't see '$line' string in: '$out'"
		fi
	done <<< "$lines"
	log_note "Successfully saw '$lines'"
}

function cleanup
{
	log_must zinject -c all
	log_must zfs destroy $TESTPOOL/4k
	log_must zfs destroy $TESTPOOL/1m
	log_must zfs destroy $TESTPOOL/$TESTVOL
}
log_onexit cleanup

log_must zfs set compression=off $TESTPOOL
log_must zfs create -o recordsize=4k $TESTPOOL/4k
log_must zfs create -o recordsize=1M $TESTPOOL/1m
log_must mkfile 1m /$TESTPOOL/4k/4k_file1
log_must mkfile 1m /$TESTPOOL/4k/4k_file2
log_must mkfile 10m /$TESTPOOL/1m/1m_file

export TESTVOL=testvol
log_must zfs create -V 100M -o compression=off -o volblocksize=4k $TESTPOOL/$TESTVOL
log_must dd if=/dev/zero of=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL bs=1M count=10

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

# Corrupt the MOS.  We do two scrubs here since the MOS error doesn't always
# show up after the first scrub for some reason.
log_must zinject -t mosdir $TESTPOOL
log_must zpool scrub $TESTPOOL
log_must wait_scrubbed $TESTPOOL
log_must zpool scrub $TESTPOOL
log_must wait_scrubbed $TESTPOOL

log_must zinject -t data -e checksum -f 100 /$TESTPOOL/4k/4k_file1
log_must zinject -t data -e checksum -f 100 /$TESTPOOL/4k/4k_file2
log_must zinject -t data -e checksum -f 100 /$TESTPOOL/1m/1m_file
log_must zinject -t data -e checksum -f 100 ${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL

# Try to read first 10 blocks of '4k_file1'.  The read should fail.
log_mustnot dd conv=fsync if=/$TESTPOOL/4k/4k_file1 of=/dev/null bs=4k count=10
log_must zpool sync

# Try to read blocks 0 and blocks 4-5 and 6-7 on '4k_file2' to create multiple
# ranges.  The read should fail.
log_mustnot dd conv=fsync if=/$TESTPOOL/4k/4k_file2 of=/dev/null bs=4k count=1 skip=0
log_must zpool sync
log_mustnot dd conv=fsync if=/$TESTPOOL/4k/4k_file2 of=/dev/null bs=4k count=2 skip=4
log_must zpool sync
log_mustnot dd conv=fsync if=/$TESTPOOL/4k/4k_file2 of=/dev/null bs=4k count=2 skip=6
log_must zpool sync

# Try to read the 2nd megabyte of '1m_file'
log_mustnot dd conv=fsync if=/$TESTPOOL/1m/1m_file of=/dev/null bs=1M skip=1 count=1
log_must zpool sync


# Try to read some ranges of the zvol.
#
# NOTE: for whatever reason, reading from the 1st megabyte of the zvol, with
# any block size, will not produce an error.  If you read past the 1st megabyte
# it will.
log_mustnot dd conv=fsync if=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL of=/dev/null bs=4k count=1 skip=1000
log_must zpool sync

log_mustnot dd conv=fsync if=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL of=/dev/null bs=4k count=3 skip=1100
log_must zpool sync

log_must zinject -c all

log_must zpool status -vv

# Look for each these lines somewhere in the zpool status output
val=$(cat << END
<metadata>:<0x1> (no ranges)
/testpool/4k/4k_file1 0-4.00K
/testpool/4k/4k_file2 0-4.00K,16K-20.0K,24K-28.0K
/testpool/1m/1m_file 1M-2.00M
testpool/testvol:<0x1> 3.91M-3.91M,4.30M-4.30M
END
)
zpool status -vv | check_status "$val"

val=$(cat << END
<metadata>:<0x1> (no ranges)
/testpool/4k/4k_file1 0-4095
/testpool/4k/4k_file2 0-4095,16384-20479,24576-28671
/testpool/1m/1m_file 1048576-2097151
testpool/testvol:<0x1> 4096000-4100095,4505600-4509695
END
)
zpool status -vvp | check_status "$val"

# Look only at the '.pools.testpool.errors' JSON object for output.  We
# remove the .object and .dataset objects since they are non-deterministic
# values.
#
# Look at four variants of the JSON output (-vj -vvj, -vvjp, -vvjp --json-int)
val=$(cat << END
{"/testpool/1m/1m_file":{"name":"/testpool/1m/1m_file"},"/testpool/4k/4k_file1":{"name":"/testpool/4k/4k_file1"},"/testpool/4k/4k_file2":{"name":"/testpool/4k/4k_file2"},"<metadata>:<0x1>":{"name":"<metadata>:<0x1>"},"testpool/testvol:<0x1>":{"name":"testpool/testvol:<0x1>"}}
END
)
zpool status -vj | jq --sort-keys -c '.pools.testpool.errors | del (.[].object,.[].dataset)' | check_status "$val"

val=$(cat << END
{"/testpool/1m/1m_file":{"block_size":"1M","name":"/testpool/1m/1m_file","object_type":"ZFS plain file","ranges":[{"end_byte":"2.00M","start_byte":"1M"}]},"/testpool/4k/4k_file1":{"block_size":"4K","name":"/testpool/4k/4k_file1","object_type":"ZFS plain file","ranges":[{"end_byte":"4.00K","start_byte":"0"}]},"/testpool/4k/4k_file2":{"block_size":"4K","name":"/testpool/4k/4k_file2","object_type":"ZFS plain file","ranges":[{"end_byte":"4.00K","start_byte":"0"},{"end_byte":"20.0K","start_byte":"16K"},{"end_byte":"28.0K","start_byte":"24K"}]},"<metadata>:<0x1>":{"name":"<metadata>:<0x1>"},"testpool/testvol:<0x1>":{"block_size":"4K","name":"testpool/testvol:<0x1>","object_type":"zvol","ranges":[{"end_byte":"3.91M","start_byte":"3.91M"},{"end_byte":"4.30M","start_byte":"4.30M"}]}}
END
)
zpool status -vvj | jq --sort-keys -c '.pools.testpool.errors | del (.[].object,.[].dataset)' | check_status "$val"

val=$(cat << END
{"/testpool/1m/1m_file":{"block_size":"1048576","name":"/testpool/1m/1m_file","object_type":"ZFS plain file","ranges":[{"end_byte":"2097151","start_byte":"1048576"}]},"/testpool/4k/4k_file1":{"block_size":"4096","name":"/testpool/4k/4k_file1","object_type":"ZFS plain file","ranges":[{"end_byte":"4095","start_byte":"0"}]},"/testpool/4k/4k_file2":{"block_size":"4096","name":"/testpool/4k/4k_file2","object_type":"ZFS plain file","ranges":[{"end_byte":"4095","start_byte":"0"},{"end_byte":"20479","start_byte":"16384"},{"end_byte":"28671","start_byte":"24576"}]},"<metadata>:<0x1>":{"name":"<metadata>:<0x1>"},"testpool/testvol:<0x1>":{"block_size":"4096","name":"testpool/testvol:<0x1>","object_type":"zvol","ranges":[{"end_byte":"4100095","start_byte":"4096000"},{"end_byte":"4509695","start_byte":"4505600"}]}}
END
)
zpool status -vvjp | jq --sort-keys -c '.pools.testpool.errors | del (.[].object,.[].dataset)' | check_status "$val"

val=$(cat << END
{"/testpool/1m/1m_file":{"block_size":1048576,"name":"/testpool/1m/1m_file","object_type":"ZFS plain file","ranges":[{"end_byte":2097151,"start_byte":1048576}]},"/testpool/4k/4k_file1":{"block_size":4096,"name":"/testpool/4k/4k_file1","object_type":"ZFS plain file","ranges":[{"end_byte":4095,"start_byte":0}]},"/testpool/4k/4k_file2":{"block_size":4096,"name":"/testpool/4k/4k_file2","object_type":"ZFS plain file","ranges":[{"end_byte":4095,"start_byte":0},{"end_byte":20479,"start_byte":16384},{"end_byte":28671,"start_byte":24576}]},"<metadata>:<0x1>":{"name":"<metadata>:<0x1>"},"testpool/testvol:<0x1>":{"block_size":4096,"name":"testpool/testvol:<0x1>","object_type":"zvol","ranges":[{"end_byte":4100095,"start_byte":4096000},{"end_byte":4509695,"start_byte":4505600}]}}
END
)
zpool status -vvjp --json-int | jq --sort-keys -c '.pools.testpool.errors | del (.[].object,.[].dataset)' | check_status "$val"

# Clear the error log from our pool
log_must zpool scrub $TESTPOOL
log_must zpool clear $TESTPOOL
log_must wait_scrubbed $TESTPOOL

log_pass "zpool status error log output is correct"
