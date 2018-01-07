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
. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.cfg
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
# 'zpool import -l' should import a pool with an encrypted dataset and load
# its key.
#
# STRATEGY:
# 1. Create an encrypted pool
# 2. Export the pool
# 3. Attempt to import the pool with the key
# 4. Verify the pool exists and the key is loaded
#

verify_runnable "both"

function cleanup
{
	destroy_pool $TESTPOOL1
	log_must rm $VDEV0
	log_must mkfile $FILE_SIZE $VDEV0
}
log_onexit cleanup

log_assert "'zpool import -l' should import a pool with an encrypted dataset" \
	"and load its key"

log_must eval "echo $PASSPHRASE | zpool create -O encryption=on" \
	"-O keyformat=passphrase -O keylocation=prompt $TESTPOOL1 $VDEV0"
log_must zpool export $TESTPOOL1
log_must eval "echo $PASSPHRASE | zpool import -l -d $DEVICE_DIR $TESTPOOL1"
log_must poolexists $TESTPOOL1
log_must key_available $TESTPOOL1
log_must mounted $TESTPOOL1

log_pass "'zpool import -l' imports a pool with an encrypted dataset and" \
	"loads its key"
