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
# Copyright (c) 2017 Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Scrubs must work on an encrypted dataset with an unloaded key.
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Generate data on the dataset
# 3. Unmount the encrypted dataset and unload its key
# 4. Start a scrub
# 5. Wait for the scrub to complete
# 6. Verify the scrub had no errors
# 7. Load the dataset key and mount it
#

verify_runnable "global"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS2 && \
		log_must zfs destroy $TESTPOOL/$TESTFS2
}
log_onexit cleanup

log_assert "Scrubs must work on an encrypted dataset with an unloaded key"

log_must eval "echo 'password' | zfs create -o encryption=on" \
	"-o keyformat=passphrase $TESTPOOL/$TESTFS2"

typeset mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS2)
log_must mkfile 10m $mntpnt/file1

for i in 2..10; do
	log_must mkfile 512b $mntpnt/file$i
done

log_must zfs unmount $TESTPOOL/$TESTFS2
log_must zfs unload-key $TESTPOOL/$TESTFS2

log_must zpool scrub -w $TESTPOOL

log_must check_pool_status $TESTPOOL "scan" "with 0 errors"

log_must eval "echo 'password' | zfs mount -l $TESTPOOL/$TESTFS2"

log_pass "Scrubs work on an encrypted dataset with an unloaded key"
