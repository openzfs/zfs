#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016, Datto, Inc. All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zfs_key/zfs_key_common.kshlib

#
# DESCRIPTION:
# 'zfs key -u' should not unload a key from the ZFS keystore
# if the dataset is busy or not encrypted.
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Attempt to unload the key
# 3. Verify the dataset was busy
# 4. Attempt to unload a key for a non encrypted dataset
# 5. Verify there was an error
#

verify_runnable "both"

function cleanup
{
	destroy_default_encrypted_dataset
}

log_onexit cleanup

log_assert "'zfs key -u' should not unload a wrapping key if the dataset \
	if the dataset is busy or not encrypted"

create_default_encrypted_dataset

log_mustnot $ZFS key -u $TESTPOOL/$CRYPTDS
check_key_available $TESTPOOL/$CRYPTDS

log_mustnot $ZFS key -u $TESTPOOL

log_pass "'zfs key -u' properly returns an error when unloading a wrapping \
	key from a busy or unencrypted dataset"
