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
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
# 'zfs load-key' should load a dataset's key from a file.
#
# STRATEGY:
# 1. Create an encrypted dataset with a key file
# 2. Unmount the dataset and unload the key
# 3. Attempt to load the dataset's key
# 4. Verify the key is loaded
# 5. Attempt to mount the dataset
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		log_must zfs destroy $TESTPOOL/$TESTFS1
}
log_onexit cleanup

log_assert "'zfs load-key' should load a key from a file"

log_must eval "echo $PASSPHRASE > /$TESTPOOL/pkey"
log_must zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=file:///$TESTPOOL/pkey $TESTPOOL/$TESTFS1

log_must zfs unmount $TESTPOOL/$TESTFS1
log_must zfs unload-key $TESTPOOL/$TESTFS1

log_must zfs load-key $TESTPOOL/$TESTFS1
log_must key_available $TESTPOOL/$TESTFS1
log_must zfs mount $TESTPOOL/$TESTFS1

log_pass "'zfs load-key' loads a key from a file"
