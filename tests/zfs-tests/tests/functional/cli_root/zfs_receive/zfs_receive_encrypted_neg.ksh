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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify 'zfs receive' fails when receiving to a dataset with unloaded keys.
#
# STRATEGY:
# 1. Create an unencrypted dataset and an encrypted dataset
# 2. Snapshot the unencrypted dataset
# 3. Unload the encrypted dataset's wrapping key
# 4. Verify that the receive operation fails without a loaded encryption key
#

verify_runnable "both"

function cleanup
{
    log_must $RM $streamfile
    log_must $ZFS destroy -r $TESTPOOL/$TESTFS1
    log_must $ZFS destroy -r $TESTPOOL/$cryptds
}

log_onexit cleanup

log_assert "Verify 'zfs receive' fails when receiving to a dataset with \
	unloaded keys."

typeset cryptds="crypt"
typeset passphrase="abcdefgh"
typeset streamfile=/var/tmp/streamfile.$$

log_must $ZFS create $TESTPOOL/$TESTFS1
log_must $ZFS snapshot $TESTPOOL/$TESTFS1@snap
log_must eval "$ZFS send $TESTPOOL/$TESTFS1@snap > $streamfile"

log_must eval "$ECHO $passphrase | \
	$ZFS create -o encryption=on -o keysource=passphrase,prompt \
	$TESTPOOL/$cryptds"
log_must $ZFS unmount $TESTPOOL/$cryptds
log_must $ZFS key -u $TESTPOOL/$cryptds
log_mustnot eval "$ZFS recv $TESTPOOL/$cryptds/recv < $streamfile"

log_pass "'zfs receive' fails when receiving to a dataset with unloaded keys."
