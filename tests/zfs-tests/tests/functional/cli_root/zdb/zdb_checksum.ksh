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
# Copyright (c) 2019 by Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# Description:
# zdb -c will display the same checksum as -ddddddbbbbbb
#
# Strategy:
# 1. Create a pool
# 2. Write some data to a file
# 3. Run zdb -ddddddbbbbbb against the file
# 4. Record the checksum and DVA of L0 block 0
# 5. Run zdb -R with :c flag and match the checksum


function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "Verify zdb -R generates the correct checksum."
log_onexit cleanup
init_data=$TESTDIR/file1
write_count=8
blksize=131072
verify_runnable "global"
verify_disk_count "$DISKS" 2

default_mirror_setup_noexit $DISKS
file_write -o create -w -f $init_data -b $blksize -c $write_count

# get object number of file
listing=$(ls -i $init_data)
set -A array $listing
obj=${array[0]}
log_note "file $init_data has object number $obj"
sync_pool $TESTPOOL

output=$(zdb -ddddddbbbbbb $TESTPOOL/$TESTFS $obj 2> /dev/null \
    | grep -m 1 "L0 DVA")
dva=$(sed -Ene 's/^.+DVA\[0\]=<([^>]+)>.*$/\1/p' <<< "$output")
log_note "block 0 of $init_data has a DVA of $dva"
cksum_expected=$(sed -Ene 's/^.+ cksum=([a-z0-9:]+)$/\1/p' <<< "$output")
log_note "expecting cksum $cksum_expected"
output=$(zdb -R $TESTPOOL $dva:c 2> /dev/null)
grep -q $cksum_expected <<<"$output" ||
	log_fail "zdb -R failed to print the correct checksum"

log_pass "zdb -R generates the correct checksum"
