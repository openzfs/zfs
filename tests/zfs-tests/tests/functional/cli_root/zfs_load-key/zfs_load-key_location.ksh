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
# 'zfs load-key -L' should override keylocation with provided value.
#
# STRATEGY:
# 1. Create a key file
# 2. Copy the key file to another location
# 3. Create an encrypted dataset using the keyfile
# 4. Unmount the dataset and unload its key
# 5. Attempt to load the dataset specifying a keylocation of file
# 6. Verify the key is loaded
# 7. Verify the keylocation is the original key file
# 8. Unload the dataset's key
# 9. Attempt to load the dataset specifying a keylocation of prompt
# 10. Verify the key is loaded
# 11. Verify the keylocation is the original key file
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1
}
log_onexit cleanup

log_assert "'zfs load-key -L' should override keylocation with provided value"

typeset key_location="/$TESTPOOL/pkey1"

log_must eval "echo $PASSPHRASE > $key_location"
log_must cp $key_location /$TESTPOOL/pkey2

log_must zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=file://$key_location $TESTPOOL/$TESTFS1

log_must zfs unmount $TESTPOOL/$TESTFS1
log_must zfs unload-key $TESTPOOL/$TESTFS1

log_must zfs load-key -L file:///$TESTPOOL/pkey2 $TESTPOOL/$TESTFS1
log_must key_available $TESTPOOL/$TESTFS1
log_must verify_keylocation $TESTPOOL/$TESTFS1 "file://$key_location"

log_must zfs unload-key $TESTPOOL/$TESTFS1
log_must eval "echo $PASSPHRASE | zfs load-key -L prompt $TESTPOOL/$TESTFS1"
log_must key_available $TESTPOOL/$TESTFS1
log_must verify_keylocation $TESTPOOL/$TESTFS1 "file://$key_location"

log_must zfs unload-key $TESTPOOL/$TESTFS1
log_must zfs load-key -L $(get_https_base_url)/PASSPHRASE $TESTPOOL/$TESTFS1
log_must key_available $TESTPOOL/$TESTFS1
log_must verify_keylocation $TESTPOOL/$TESTFS1 "file://$key_location"

log_pass "'zfs load-key -L' overrides keylocation with provided value"
