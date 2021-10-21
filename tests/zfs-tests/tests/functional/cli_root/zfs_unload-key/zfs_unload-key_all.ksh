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
# 'zfs unload-key -a' should unload keys for all datasets.
#
# STRATEGY:
# 1. Create an encrypted filesystem, encrypted child dataset, an encrypted
#    zvol, and an encrypted pool
# 2. Unmount all datasets
# 3. Attempt to unload all dataset keys
# 4. Verify each dataset has its key unloaded
# 5. Attempt to mount each dataset
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && destroy_dataset $TESTPOOL/$TESTFS1 -r
	datasetexists $TESTPOOL/zvol && destroy_dataset $TESTPOOL/zvol
	poolexists $TESTPOOL1 && log_must destroy_pool $TESTPOOL1
}
log_onexit cleanup

log_assert "'zfs unload-key -a' should unload keys for all datasets"

log_must eval "echo $PASSPHRASE1 > /$TESTPOOL/pkey"
log_must zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=file:///$TESTPOOL/pkey $TESTPOOL/$TESTFS1
log_must zfs create $TESTPOOL/$TESTFS1/child

log_must zfs create -V 64M -o encryption=on -o keyformat=passphrase \
	-o keylocation=file:///$TESTPOOL/pkey $TESTPOOL/zvol

typeset DISK2="$(echo $DISKS | awk '{ print $2}')"
log_must zpool create -O encryption=on -O keyformat=passphrase \
	-O keylocation=file:///$TESTPOOL/pkey $TESTPOOL1 $DISK2

log_must zfs unmount $TESTPOOL/$TESTFS1
log_must zfs unmount $TESTPOOL1

log_must_busy zfs unload-key -a

log_must key_unavailable $TESTPOOL/$TESTFS1
log_must key_unavailable $TESTPOOL/$TESTFS1/child
log_must key_unavailable $TESTPOOL/zvol
log_must key_unavailable $TESTPOOL1

log_mustnot zfs mount $TESTPOOL
log_mustnot zfs mount $TESTPOOL/zvol
log_mustnot zfs mount $TESTPOOL/$TESTFS1

log_pass "'zfs unload-key -a' unloads keys for all datasets"
