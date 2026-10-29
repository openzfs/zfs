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
# 5. Verify -v prints file names without trailing whitespace, -j reports
#    the error count but no error list, and an unprivileged user who
#    cannot read the error log still sees the correct error count

verify_runnable "both"

log_assert "Verify zpool status prints error log ranges"

ZS_USER="zsuser"
ZS_GROUP="zsgroup"

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
	log_must restore_tunable VOL_PREFETCH_BYTES
	log_must restore_tunable PREFETCH_DISABLE
	log_must zinject -c all
	destroy_dataset $TESTPOOL/4k
	destroy_dataset $TESTPOOL/1m
	destroy_dataset $TESTPOOL/$TESTVOL
	del_user $ZS_USER
	del_group $ZS_GROUP
}

# Print the error count from the text 'zpool status' output on stdin.
function text_errcount
{
	awk '/^errors: No known data errors/ {print 0}
	    /^errors: [0-9]+ data errors/ {print $2}'
}

# Print the '.pools.testpool.<key>' value from the JSON output on stdin.
function json_val # <key>
{
	jq -c --arg k "$1" '.pools.testpool[$k]'
}

log_onexit cleanup
log_must save_tunable PREFETCH_DISABLE
log_must set_tunable32 PREFETCH_DISABLE 1
log_must save_tunable VOL_PREFETCH_BYTES
log_must set_tunable32 VOL_PREFETCH_BYTES 0

log_must zfs set compression=off $TESTPOOL
log_must zfs create -o recordsize=4k $TESTPOOL/4k
log_must zfs create -o recordsize=1M $TESTPOOL/1m
log_must mkfile 1m /$TESTPOOL/4k/4k_file1
log_must mkfile 1m /$TESTPOOL/4k/4k_file2
log_must mkfile 10m /$TESTPOOL/1m/1m_file

export TESTVOL=testvol
log_must zfs create -V 200M -o compression=off -o volblocksize=4k $TESTPOOL/$TESTVOL
block_device_wait ${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL
log_must dd if=/dev/zero of=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL bs=1M count=10 conv=fsync
log_must zpool sync $TESTPOOL

log_must_busy zpool export $TESTPOOL
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
log_mustnot dd conv=fsync if=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL of=/dev/null bs=4k count=1 skip=1000
log_must zpool sync

log_mustnot dd conv=fsync if=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL of=/dev/null bs=4k count=3 skip=1100
log_must zpool sync

log_must zinject -c all

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

# Look only at the '.pools.testpool.errlist' JSON object for output.  We
# remove the .object and .dataset objects since they are non-deterministic
# values.
#
# Look at four variants of the JSON output (-vj -vvj, -vvjp, -vvjp --json-int)
val=$(cat << END
["/testpool/1m/1m_file","/testpool/4k/4k_file1","/testpool/4k/4k_file2","<metadata>:<0x1>","testpool/testvol:<0x1>"]
END
)
zpool status -vj | jq -c '.pools.testpool.errlist | sort' | check_status "$val"

val=$(cat << END
[{"/testpool/1m/1m_file":{"object_type":"ZFS plain file","ranges":[{"start_byte":"1M","end_byte":"2.00M"}],"name":"/testpool/1m/1m_file","block_size":"1M"}},{"/testpool/4k/4k_file1":{"object_type":"ZFS plain file","ranges":[{"start_byte":"0","end_byte":"4.00K"}],"name":"/testpool/4k/4k_file1","block_size":"4K"}},{"/testpool/4k/4k_file2":{"object_type":"ZFS plain file","ranges":[{"start_byte":"0","end_byte":"4.00K"},{"start_byte":"16K","end_byte":"20.0K"},{"start_byte":"24K","end_byte":"28.0K"}],"name":"/testpool/4k/4k_file2","block_size":"4K"}},{"<metadata>:<0x1>":{"name":"<metadata>:<0x1>"}},{"testpool/testvol:<0x1>":{"object_type":"zvol","ranges":[{"start_byte":"3.91M","end_byte":"3.91M"},{"start_byte":"4.30M","end_byte":"4.30M"}],"name":"testpool/testvol:<0x1>","block_size":"4K"}}]
END
)
zpool status -vvj | jq -c '.pools.testpool.errlist | map(del(.[].dataset)) | map(del(.[].object)) | sort' | check_status "$val"

val=$(cat << END
[{"/testpool/1m/1m_file":{"object_type":"ZFS plain file","ranges":[{"start_byte":"1048576","end_byte":"2097151"}],"name":"/testpool/1m/1m_file","block_size":"1048576"}},{"/testpool/4k/4k_file1":{"object_type":"ZFS plain file","ranges":[{"start_byte":"0","end_byte":"4095"}],"name":"/testpool/4k/4k_file1","block_size":"4096"}},{"/testpool/4k/4k_file2":{"object_type":"ZFS plain file","ranges":[{"start_byte":"0","end_byte":"4095"},{"start_byte":"16384","end_byte":"20479"},{"start_byte":"24576","end_byte":"28671"}],"name":"/testpool/4k/4k_file2","block_size":"4096"}},{"<metadata>:<0x1>":{"name":"<metadata>:<0x1>"}},{"testpool/testvol:<0x1>":{"object_type":"zvol","ranges":[{"start_byte":"4096000","end_byte":"4100095"},{"start_byte":"4505600","end_byte":"4509695"}],"name":"testpool/testvol:<0x1>","block_size":"4096"}}]
END
)
zpool status -vvjp | jq -c '.pools.testpool.errlist | map(del(.[].dataset)) | map(del(.[].object)) | sort' | check_status "$val"

val=$(cat << END
[{"/testpool/1m/1m_file":{"object_type":"ZFS plain file","ranges":[{"start_byte":1048576,"end_byte":2097151}],"name":"/testpool/1m/1m_file","block_size":1048576}},{"/testpool/4k/4k_file1":{"object_type":"ZFS plain file","ranges":[{"start_byte":0,"end_byte":4095}],"name":"/testpool/4k/4k_file1","block_size":4096}},{"/testpool/4k/4k_file2":{"object_type":"ZFS plain file","ranges":[{"start_byte":0,"end_byte":4095},{"start_byte":16384,"end_byte":20479},{"start_byte":24576,"end_byte":28671}],"name":"/testpool/4k/4k_file2","block_size":4096}},{"<metadata>:<0x1>":{"name":"<metadata>:<0x1>"}},{"testpool/testvol:<0x1>":{"object_type":"zvol","ranges":[{"start_byte":4096000,"end_byte":4100095},{"start_byte":4505600,"end_byte":4509695}],"name":"testpool/testvol:<0x1>","block_size":4096}}]
END
)
zpool status -vvjp --json-int | jq -c '.pools.testpool.errlist | map(del(.[].dataset)) | map(del(.[].object)) | sort' | check_status "$val"

# With -v, a file name is the last thing on its line; the space before
# the ranges is only printed with -vv.
log_must eval "zpool status -v $TESTPOOL |" \
    "grep -q '^[[:space:]]*/testpool/4k/4k_file1\$'"
log_mustnot eval "zpool status -v $TESTPOOL |" \
    "grep -q '4k_file1[[:space:]]\$'"

nerr=$(zpool status $TESTPOOL | text_errcount)
log_note "text error count: $nerr"
if [[ -z "$nerr" ]] || (( nerr == 0 )); then
	log_fail "No data errors reported in 'zpool status $TESTPOOL'"
fi

# -j always reports the error count, but only includes the list with -v.
out=$(zpool status -jp --json-int $TESTPOOL)
[[ "$(echo "$out" | json_val error_count)" == "$nerr" ]] || \
	log_fail "'zpool status -j' error_count is not $nerr"
[[ "$(echo "$out" | json_val errlist)" == "null" ]] || \
	log_fail "'zpool status -j' unexpectedly includes errlist"

out=$(zpool status -jvp --json-int $TESTPOOL)
[[ "$(echo "$out" | json_val error_count)" == "$nerr" ]] || \
	log_fail "'zpool status -jv' error_count is not $nerr"
[[ "$(echo "$out" | json_val errlist)" != "null" ]] || \
	log_fail "'zpool status -jv' does not include errlist"

#
# An unprivileged user can run 'zpool status' but is not allowed to read
# the error log.  The error count must still be reported correctly.
#
log_must add_group $ZS_GROUP
log_must add_user $ZS_GROUP $ZS_USER

if user_run $ZS_USER zpool status $TESTPOOL ; then
	[[ "$(text_errcount < $TEST_BASE_DIR/out)" == "$nerr" ]] || \
		log_fail "Unprivileged 'zpool status' error count is not $nerr"

	for flags in "-jp --json-int" "-jvp --json-int"; do
		log_must user_run $ZS_USER zpool status $flags $TESTPOOL
		[[ "$(json_val error_count < $TEST_BASE_DIR/out)" == "$nerr" ]] || \
			log_fail "Unprivileged 'zpool status $flags'" \
			    "error_count is not $nerr"
		[[ "$(json_val errlist < $TEST_BASE_DIR/out)" == "null" ]] || \
			log_fail "Unprivileged 'zpool status $flags'" \
			    "includes errlist"
	done
else
	log_note "$ZS_USER cannot run zpool status, skipping unprivileged checks"
fi

# Clear the error log from our pool
log_must zpool scrub $TESTPOOL
log_must zpool clear $TESTPOOL
log_must wait_scrubbed $TESTPOOL

log_pass "zpool status error log output is correct"
