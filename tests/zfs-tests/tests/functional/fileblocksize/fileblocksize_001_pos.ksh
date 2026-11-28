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
#	FS_IOC_FSSETXATTR with FS_XFLAG_EXTSIZE fixes the block size of an
#	empty regular file, which then keeps it as the file grows.
#
# STRATEGY:
#	1. Fix each block size from 512 bytes to 128K on an empty file, write
#	   1 MiB, and check the block size and the reported setting.
#	2. Check that a file without the setting grows to recordsize.
#	3. Check that truncating to zero and rewriting keeps the block size.
#	4. Check that the setting survives export and import.
#	5. Check that clearing it on a one-block file lets the file grow.
#	6. Check that a block size above the dataset's recordsize is refused.
#

. $STF_SUITE/tests/functional/fileblocksize/fileblocksize.kshlib

verify_runnable "global"

function cleanup
{
	rm -f $TESTDIR/f.* $TESTDIR/plain $TESTDIR/cleared
	datasetexists $TESTPOOL/$TESTFS/small &&
	    destroy_dataset $TESTPOOL/$TESTFS/small
}

log_onexit cleanup
log_assert "fsx_extsize fixes the block size of an empty file"

typeset -i size
for size in 512 4096 8192 16384 32768 65536 131072; do
	log_must touch $TESTDIR/f.$size
	log_must extsize -s $size $TESTDIR/f.$size
	verify_ext $TESTDIR/f.$size extsize $size
	write_mib $TESTDIR/f.$size
	verify_blksz $TESTDIR/f.$size $size
	# setting the same size again on a file with data is a no-op
	log_must extsize -s $size $TESTDIR/f.$size
done

write_mib $TESTDIR/plain
verify_blksz $TESTDIR/plain 131072
verify_ext $TESTDIR/plain none 0

log_must truncate -s 0 $TESTDIR/f.16384
write_mib $TESTDIR/f.16384
verify_blksz $TESTDIR/f.16384 16384

typeset sum=$(xxh128digest $TESTDIR/f.4096)
reimport_pool
verify_ext $TESTDIR/f.4096 extsize 4096
verify_blksz $TESTDIR/f.4096 4096
[[ "$(xxh128digest $TESTDIR/f.4096)" == "$sum" ]] ||
    log_fail "f.4096 changed across export and import"
log_must dd if=/dev/urandom of=$TESTDIR/f.4096 bs=128k count=8 seek=8 \
    conv=notrunc 2>/dev/null
verify_blksz $TESTDIR/f.4096 4096

log_must touch $TESTDIR/cleared
log_must extsize -s 8192 $TESTDIR/cleared
log_must dd if=/dev/urandom of=$TESTDIR/cleared bs=4k count=1 2>/dev/null
log_must extsize -c $TESTDIR/cleared
verify_ext $TESTDIR/cleared none 0
log_must dd if=/dev/urandom of=$TESTDIR/cleared bs=128k count=8 \
    conv=notrunc 2>/dev/null
verify_blksz $TESTDIR/cleared 131072

log_must zfs create -o recordsize=16k $TESTPOOL/$TESTFS/small
typeset small=$(get_prop mountpoint $TESTPOOL/$TESTFS/small)
log_must touch $small/big
extsize_fails EINVAL -s 32768 $small/big
verify_ext $small/big none 0
log_must extsize -s 16384 $small/big
write_mib $small/big
verify_blksz $small/big 16384

log_pass "fsx_extsize fixes the block size of an empty file"
