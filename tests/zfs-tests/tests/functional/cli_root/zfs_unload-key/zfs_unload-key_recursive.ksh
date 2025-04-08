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
# 'zfs unload-key -r' should recursively unload keys.
#
# STRATEGY:
# 1. Create a parent encrypted dataset
# 2. Create a sibling encrypted dataset
# 3. Create a child dataset as an encryption root
# 4. Unmount all datasets
# 5. Attempt to unload all dataset keys under parent
# 6. Verify parent and child have their keys unloaded
# 7. Verify sibling has its key loaded
# 8. Attempt to mount all datasets
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1 -r
}
log_onexit cleanup

log_assert "'zfs unload-key -r' should recursively unload keys"

log_must eval "echo $PASSPHRASE > /$TESTPOOL/pkey"
log_must zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=file:///$TESTPOOL/pkey $TESTPOOL/$TESTFS1
log_must zfs create -o keyformat=passphrase \
	-o keylocation=file:///$TESTPOOL/pkey $TESTPOOL/$TESTFS1/child
log_must eval "echo $PASSPHRASE1 | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/$TESTFS2"

log_must zfs unmount $TESTPOOL/$TESTFS1
log_must zfs unmount $TESTPOOL/$TESTFS2

log_must zfs unload-key -r $TESTPOOL/$TESTFS1

log_must key_unavailable $TESTPOOL/$TESTFS1
log_must key_unavailable $TESTPOOL/$TESTFS1/child

log_must key_available $TESTPOOL/$TESTFS2

log_mustnot zfs mount $TESTPOOL/$TESTFS1
log_mustnot zfs mount $TESTPOOL/$TESTFS1/child
log_must zfs mount $TESTPOOL/$TESTFS2

log_pass "'zfs unload-key -r' recursively unloads keys"
