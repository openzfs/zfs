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
# 'zfs load-key -n' should load the key for an already loaded dataset.
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Attempt to load the dataset's key
# 3. Verify the key is loaded
# 4. Attempt to load the dataset's key with an invalid key
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1
}
log_onexit cleanup

log_assert "'zfs load-key -n' should load the key for a loaded dataset"

log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/$TESTFS1"

log_must eval "echo $PASSPHRASE | zfs load-key -n $TESTPOOL/$TESTFS1"
log_must key_available $TESTPOOL/$TESTFS1

log_mustnot eval "echo $PASSPHRASE1 | zfs load-key -n $TESTPOOL/$TESTFS1"

log_pass "'zfs load-key -n' loads the key for a loaded dataset"
