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
# Copyright (c) 2017 Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
# 'zfs change-key' should change the key material.
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Attempt to change the key
# 3. Unmount the dataset and unload its key
# 4. Attempt to load the old key
# 5. Verify the key is not loaded
# 6. Attempt to load the new key
# 7. Verify the key is loaded
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1 -f
}
log_onexit cleanup

log_assert "'zfs change-key' should change the key material"

log_must eval "echo $PASSPHRASE1 | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/$TESTFS1"
log_must eval "echo $PASSPHRASE2 | zfs change-key $TESTPOOL/$TESTFS1"

log_must zfs unmount $TESTPOOL/$TESTFS1
log_must zfs unload-key $TESTPOOL/$TESTFS1

log_mustnot eval "echo $PASSPHRASE1 | zfs load-key $TESTPOOL/$TESTFS1"
log_must key_unavailable $TESTPOOL/$TESTFS1

log_must eval "echo $PASSPHRASE2 | zfs load-key $TESTPOOL/$TESTFS1"
log_must key_available $TESTPOOL/$TESTFS1

log_pass "'zfs change-key' changes the key material"
