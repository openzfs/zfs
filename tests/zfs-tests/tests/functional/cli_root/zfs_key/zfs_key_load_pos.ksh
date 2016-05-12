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
# 'zfs key -l' should load a valid key into the ZFS keystore.
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Unmount and unload the dataset's key
# 3. Attempt to load the key
# 4. Verify the key is loaded correctly
#

verify_runnable "both"

function cleanup
{
	destroy_default_encrypted_dataset
}

log_onexit cleanup

log_assert "'zfs key -l' should properly load a valid wrapping key"

create_default_encrypted_dataset
log_must $ZFS unmount $TESTPOOL/$CRYPTDS
log_must $ZFS key -u $TESTPOOL/$CRYPTDS
log_must eval '$ECHO $PKEY | $ZFS key -l $TESTPOOL/$CRYPTDS'
check_key_available $TESTPOOL/$CRYPTDS

log_pass "'zfs key -l' properly loads a valid wrapping key"
