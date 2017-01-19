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
# Verify 'zfs send' can perform unencrypted sends from encrypted datasets.
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Snapshot the dataset
# 3. Verify sending the stream succeeds
#

verify_runnable "both"

function cleanup
{
    datasetexists $TESTPOOL/$cryptds && \
        log_must $ZFS destroy -r $TESTPOOL/$cryptds
}

log_onexit cleanup

log_assert "Verify 'zfs send' can perform unencrypted sends from \
	encrypted datasets."

typeset cryptds="crypt"
typeset passphrase="abcdefgh"
typeset snap="$TESTPOOL/$cryptds@snap"

log_must eval "$ECHO $passphrase | \
	$ZFS create -o encryption=on -o keysource=passphrase,prompt \
	$TESTPOOL/$cryptds"
log_must $ZFS snapshot $snap
log_must eval "$ZFS send $snap > /dev/null"

log_pass "'zfs send' can perform unencrypted sends from encrypted datasets."
