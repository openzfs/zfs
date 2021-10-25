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
# 'zfs change-key -o' should change the keylocation.
#
# STRATEGY:
# 1. Create an encryption dataset with a file key location
# 2. Change the key location to 'prompt'
# 3. Verify the key location
# 4. Unmount the dataset and unload its key
# 5. Attempt to load the dataset's key
# 6. Attempt to change the key location to 'none'
# 7. Attempt to change the key location to an invalid value
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1 -f
}
log_onexit cleanup

log_assert "'zfs change-key -o' should change the keylocation"

log_must eval "echo $PASSPHRASE > /$TESTPOOL/pkey"
log_must zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=file:///$TESTPOOL/pkey $TESTPOOL/$TESTFS1
log_must verify_keylocation $TESTPOOL/$TESTFS1 "file:///$TESTPOOL/pkey"

log_must eval "echo $PASSPHRASE1 | zfs change-key -o keylocation=prompt" \
	"$TESTPOOL/$TESTFS1"
log_must verify_keylocation $TESTPOOL/$TESTFS1 "prompt"

log_must zfs unmount $TESTPOOL/$TESTFS1
log_must zfs unload-key $TESTPOOL/$TESTFS1
log_must eval "echo $PASSPHRASE1 | zfs load-key $TESTPOOL/$TESTFS1"

log_mustnot zfs change-key -o keylocation=none $TESTPOOL/$TESTFS1
log_mustnot zfs change-key -o keylocation=foobar $TESTPOOL/$TESTFS1

log_pass "'zfs change-key -o' changes the keylocation"
