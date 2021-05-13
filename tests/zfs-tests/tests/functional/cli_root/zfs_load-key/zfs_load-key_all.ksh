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
# 'zfs load-key -a' should load keys for all datasets.
#
# STRATEGY:
# 1. Create an encrypted filesystem, encrypted zvol, and an encrypted pool
# 2. Unmount all datasets and unload their keys
# 3. Attempt to load all dataset keys
# 4. Verify each dataset has its key loaded
# 5. Attempt to mount the pool and filesystem
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && destroy_dataset $TESTPOOL/$TESTFS1
	datasetexists $TESTPOOL/$TESTFS2 && destroy_dataset $TESTPOOL/$TESTFS2
	datasetexists $TESTPOOL/zvol && destroy_dataset $TESTPOOL/zvol
	poolexists $TESTPOOL1 && log_must destroy_pool $TESTPOOL1
}
log_onexit cleanup

log_assert "'zfs load-key -a' should load keys for all datasets"

log_must eval "echo $PASSPHRASE1 > /$TESTPOOL/pkey"
log_must zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=file:///$TESTPOOL/pkey $TESTPOOL/$TESTFS1

log_must zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=$(get_https_base_url)/PASSPHRASE $TESTPOOL/$TESTFS2

log_must zfs create -V 64M -o encryption=on -o keyformat=passphrase \
	-o keylocation=file:///$TESTPOOL/pkey $TESTPOOL/zvol

typeset DISK2="$(echo $DISKS | awk '{ print $2}')"
log_must zpool create -O encryption=on -O keyformat=passphrase \
	-O keylocation=file:///$TESTPOOL/pkey $TESTPOOL1 $DISK2

log_must zfs unmount $TESTPOOL/$TESTFS1
log_must_busy zfs unload-key $TESTPOOL/$TESTFS1

log_must zfs unmount $TESTPOOL/$TESTFS2
log_must_busy zfs unload-key $TESTPOOL/$TESTFS2

log_must_busy zfs unload-key $TESTPOOL/zvol

log_must zfs unmount $TESTPOOL1
log_must_busy zfs unload-key $TESTPOOL1

log_must zfs load-key -a

log_must key_available $TESTPOOL1
log_must key_available $TESTPOOL/zvol
log_must key_available $TESTPOOL/$TESTFS1
log_must key_available $TESTPOOL/$TESTFS2

log_must zfs mount $TESTPOOL1
log_must zfs mount $TESTPOOL/$TESTFS1
log_must zfs mount $TESTPOOL/$TESTFS2

log_pass "'zfs load-key -a' loads keys for all datasets"
