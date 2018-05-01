#!/bin/ksh -p
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

#
# Copyright 2018, Datto Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# 'zfs diff' should work with encrypted datasets
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Create two snapshots of the dataset
# 3. Perform 'zfs diff -Ft' and verify no errors occur
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
		log_must zfs destroy -r $TESTPOOL/$TESTFS1
}

log_assert "'zfs diff' should work with encrypted datasets"
log_onexit cleanup

# 1. Create an encrypted dataset
log_must eval "echo 'password' | zfs create -o encryption=on \
	-o keyformat=passphrase $TESTPOOL/$TESTFS1"
MNTPOINT="$(get_prop mountpoint $TESTPOOL/$TESTFS1)"

# 2. Create two snapshots of the dataset
log_must zfs snapshot $TESTPOOL/$TESTFS1@snap1
log_must touch "$MNTPOINT/file"
log_must zfs snapshot $TESTPOOL/$TESTFS1@snap2

# 3. Perform 'zfs diff' and verify no errors occur
log_must zfs diff -Ft $TESTPOOL/$TESTFS1@snap1 $TESTPOOL/$TESTFS1@snap2

log_pass "'zfs diff' works with encrypted datasets"
