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
# Copyright (c) 2026, Klara Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# Description:
# zdb -fHv <dataset> <objnum> will display block
# layouts for the object.
#
# Strategery:
# 1. Create a RAIDZ2 pool, set compression to none
# 2. Create a file filled with random data
# 3. Get the inode number of the file
# 4. Run zdb -fHv <pool>/ <inum> & extract file
# 5. Compare real file and extracted file.

DATA=/$TESTPOOL1/random.bin
BLOCKS=$(( $RANDOM % 16 ))
COMPARE=/tmp/compare.$$

function cleanup
{
    destroy_pool $TESTPOOL1
    rm -f $TESTDIR/file?.bin $COMPARE
}

log_assert "Verify zdb -fHv displays correct offsets"
log_onexit cleanup

# 1. Create a RAIDZ1 pool
log_must mkdir -p $TESTDIR
for file in 1 2 3 4 5 6
do
    rm -f $TESTDIR/file${file}.bin
    touch $TESTDIR/file${file}.bin
    log_must truncate -s 128m $TESTDIR/file${file}.bin
done

log_must zpool create -O compression=off -O recordsize=16K $TESTPOOL1 raidz2 $TESTDIR/file[123456].bin
zfs get compression,recordsize $TESTPOOL1
# 2. Create a file with random data
log_must rm -f $DATA
log_must dd if=/dev/urandom of=${DATA} bs=16k count=${BLOCKS} > /dev/null 2>&1
log_must zpool sync $TESTPOOL1

# 3. Get the inode number of the file
INUM=$(ls -li $DATA | cut -f1 -d ' ')

# 4. Extract the contents of the file using dd
rm -f $COMPARE
log_must touch ${COMPARE}
log_must zdb -fHv $TESTPOOL1/ ${INUM} |  grep 'D.$' |
    while read file offset count rest
    do
	log_must sh -c "dd if=$TESTDIR/${file} bs=512 skip=${offset} count=${count} >> ${COMPARE}"
    done

# 5. Compare files
log_must cmp  ${COMPARE} ${DATA}

log_pass "'zdb -fHv' works as expected."
