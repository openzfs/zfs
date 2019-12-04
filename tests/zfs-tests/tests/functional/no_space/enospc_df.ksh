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
# Copyright (c) 2014, 2016 by Delphix. All rights reserved.
# Copyright (c) 2019 by Datto Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/no_space/enospc.cfg

#
# DESCRIPTION:
# After filling a filesystem, the df command produces the
# expected result against the pool root filesystem.
#
# STRATEGY:
# 1. Write a file until the child file system is full.
# 2. Ensure that ENOSPC is returned.
# 3. Unmount the child file system.
# 4. Issue df -h command.
# 5. Ensure pool root filesystem is included (issue #8253).
# 6. Issue df -h <filesystem>.
# 7. Ensure size and used are non-zero.
#

verify_runnable "both"

log_onexit default_cleanup_noexit

log_assert "Correct df output is returned when file system is full."

default_setup_noexit $DISK_SMALL
log_must zfs set compression=off $TESTPOOL/$TESTFS

log_note "Writing file: $TESTFILE0 until ENOSPC."
file_write -o create -f $TESTDIR/$TESTFILE0 -b $BLOCKSZ \
    -c $NUM_WRITES -d $DATA
ret=$?

(( $ret != $ENOSPC )) && \
    log_fail "$TESTFILE0 returned: $ret rather than ENOSPC."

log_must zfs umount $TESTPOOL/$TESTFS

# Ensure the pool root filesystem shows in df output.
# If the pool was full (available == 0) and the pool
# root filesystem had very little in it (used < 1 block),
# the size reported to df was zero (issue #8253) and
# df skipped the filesystem in its output.
log_must eval "df -h | grep $TESTPOOL"

# Confirm df size and used are non-zero.
size=$(df -h /$TESTPOOL | grep $TESTPOOL | awk '{print $2}')
used=$(df -h /$TESTPOOL | grep $TESTPOOL | awk '{print $3}')
if [[ "$size" = "0" ]] || [[ "$used" = "0" ]]
then
	log_fail "df failed with size $size and used $used."
fi
log_pass "df after ENOSPC works as expected."
