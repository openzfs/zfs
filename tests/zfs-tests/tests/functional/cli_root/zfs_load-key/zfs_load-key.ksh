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
# 'zfs load-key' should only load a key for an unloaded encrypted dataset.
#
# STRATEGY:
# 1. Attempt to load the default dataset's key
# 2. Unmount the dataset
# 3. Attempt to load the default dataset's key
# 4. Create an encrypted dataset
# 5. Unmount the dataset and unload its key
# 6. Attempt to load the dataset's key
# 7. Verify the dataset's key is loaded
# 8. Attempt to load the dataset's key again
# 9. Create an encrypted pool
# 10. Unmount the pool and unload its key
# 11. Attempt to load the pool's key
# 12. Verify the pool's key is loaded
# 13. Attempt to load the pool's key again
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1
	poolexists $TESTPOOL1 && log_must destroy_pool $TESTPOOL1
}
log_onexit cleanup

log_assert "'zfs load-key' should only load the key for an" \
	"unloaded encrypted dataset"

log_mustnot eval "echo $PASSPHRASE | zfs load-key $TESTPOOL/$TESTFS"

log_must zfs unmount $TESTPOOL/$TESTFS
log_mustnot eval "echo $PASSPHRASE | zfs load-key $TESTPOOL/$TESTFS"

log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase -o keylocation=prompt $TESTPOOL/$TESTFS1"

log_must zfs unmount $TESTPOOL/$TESTFS1
log_must zfs unload-key $TESTPOOL/$TESTFS1

log_must eval "echo $PASSPHRASE | zfs load-key $TESTPOOL/$TESTFS1"
log_must key_available $TESTPOOL/$TESTFS1

log_mustnot eval "echo $PASSPHRASE | zfs load-key $TESTPOOL/$TESTFS1"

typeset DISK2="$(echo $DISKS | awk '{ print $2 }')"
log_must eval "echo $PASSPHRASE | zpool create -O encryption=on" \
	"-O keyformat=passphrase -O keylocation=prompt $TESTPOOL1 $DISK2"

log_must zfs unmount $TESTPOOL1
log_must zfs unload-key $TESTPOOL1

log_must eval "echo $PASSPHRASE | zfs load-key $TESTPOOL1"
log_must key_available $TESTPOOL1

log_mustnot eval "echo $PASSPHRASE | zfs load-key $TESTPOOL1"

log_pass "'zfs load-key' only loads the key for an unloaded encrypted dataset"
