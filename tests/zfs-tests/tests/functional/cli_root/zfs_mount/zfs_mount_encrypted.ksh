#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright (c) 2017, Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
# 'zfs mount -l' should accept a valid key as it mounts the filesystem.
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Unmount and unload the dataset's key
# 3. Verify the key is unloaded
# 4. Attempt to mount all datasets in the pool
# 5. Verify that no error code is produced
# 6. Verify that the encrypted dataset is not mounted
# 7. Attempt to load the key while mounting the dataset
# 8. Verify the key is loaded
# 9. Verify the dataset is mounted
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1 -f
}

log_onexit cleanup

log_assert "'zfs mount -l' should properly load a valid wrapping key"

log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase $TESTPOOL/$TESTFS1"

log_must zfs unmount $TESTPOOL/$TESTFS1
log_must zfs unload-key $TESTPOOL/$TESTFS1
log_must key_unavailable $TESTPOOL/$TESTFS1

log_must zfs mount -a
unmounted $TESTPOOL/$TESTFS1 || \
	log_fail "Filesystem $TESTPOOL/$TESTFS1 is mounted"

log_must eval "echo $PASSPHRASE | zfs mount -l $TESTPOOL/$TESTFS1"
log_must key_available $TESTPOOL/$TESTFS1

mounted $TESTPOOL/$TESTFS1 || \
	log_fail "Filesystem $TESTPOOL/$TESTFS1 is unmounted"

log_pass "'zfs mount -l' properly loads a valid wrapping key"
