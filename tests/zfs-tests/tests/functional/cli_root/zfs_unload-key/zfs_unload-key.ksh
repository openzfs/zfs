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
# 'zfs unload-key' should only unload the key of an unmounted dataset.
#
# STRATEGY:
# 1. Attempt to unload the default dataset's key
# 2. Unmount the dataset
# 3. Attempt to unload the default dataset's key
# 4. Create an encrypted dataset
# 5. Attempt to unload the dataset's key
# 6. Verify the key is loaded
# 7. Unmount the dataset
# 8. Attempt to unload the dataset's key
# 9. Verify the key is not loaded
# 10. Attempt to unload the dataset's key
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1
}
log_onexit cleanup

log_assert "'zfs unload-key' should unload the key for an unmounted" \
	"encrypted dataset"

log_mustnot zfs unload-key $TESTPOOL/$TESTFS

log_must zfs unmount $TESTPOOL/$TESTFS
log_mustnot zfs unload-key $TESTPOOL/$TESTFS

log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/$TESTFS1"
log_mustnot zfs unload-key $TESTPOOL/$TESTFS1
log_must key_available $TESTPOOL/$TESTFS1

log_must zfs unmount $TESTPOOL/$TESTFS1
log_must zfs unload-key $TESTPOOL/$TESTFS1
log_must key_unavailable $TESTPOOL/$TESTFS1

log_mustnot zfs unload-key $TESTPOOL/$TESTFS1

log_pass "'zfs unload-key' unloads the key for an unmounted encrypted dataset"
