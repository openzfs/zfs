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
# Verify 'zfs receive' works with encrypted datasets.
#
# STRATEGY:
# 1. Create an unencrypted dataset with some data
# 2. Snapshot the unencrypted dataset
# 3. Create an encrypted dataset
# 4. Verify that the send stream is receivable as an encrypted child dataset
# 5. Verify that the data that was sent matches the original data
#

verify_runnable "both"

function cleanup
{
    log_must $RM $streamfile
    log_must $ZFS destroy -r $TESTPOOL/$TESTFS1
    log_must $ZFS destroy -r $TESTPOOL/$cryptds
}

log_onexit cleanup

log_assert "Verify 'zfs receive' works with encrypted datasets."

typeset cryptds="crypt"
typeset passphrase="abcdefgh"
typeset testfile="testfile"
typeset streamfile=/var/tmp/streamfile.$$

log_must $ZFS create $TESTPOOL/$TESTFS1
send_mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS1) || \
	log_fail "get_prop mountpoint $TESTPOOL/$TESTFS1"

log_must $FILE_WRITE -o create -f $send_mntpnt/$testfile -b 4096 -c 5 -d 1
log_must $ZFS snapshot $TESTPOOL/$TESTFS1@snap
log_must eval "$ZFS send $TESTPOOL/$TESTFS1@snap > $streamfile"

log_must eval "$ECHO $passphrase | \
	$ZFS create -o encryption=on -o keysource=passphrase,prompt \
	$TESTPOOL/$cryptds"
log_must eval "$ZFS recv $TESTPOOL/$cryptds/recv < $streamfile"

recv_mntpnt=$(get_prop mountpoint $TESTPOOL/$cryptds/recv) || \
	log_fail "get_prop mountpoint $TESTPOOL/$cryptds/recv"

checksum1=$($SUM $send_mntpnt/$testfile | $AWK '{print $1}')
checksum2=$($SUM $recv_mntpnt/$testfile | $AWK '{print $1}')
[[ "$checksum1" != "$checksum2" ]] && \
	log_fail "Checksums differ ($checksum1 != $checksum2)"

log_pass "'zfs receive' works with encrypted datasets."
