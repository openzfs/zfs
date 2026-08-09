#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright (c) 2019, by Delphix. All rights reserved.
# Copyright (c) 2021, George Amanakis. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify feature@head_errlog=disabled works.
#
# STRATEGY:
# 1. Create a pool with feature@head_errlog=disabled and a file
# 2. zinject checksum errors
# 3. Read the file
# 4. Take a snapshot and make a clone
# 5. Verify that zpool status displays the old behaviour.

function cleanup
{
	log_must zinject -c all
	datasetexists $TESTPOOL2 && log_must zpool destroy $TESTPOOL2
	rm -f $TESTDIR/vdev_a
}

verify_runnable "both"

log_assert "Verify 'zpool status -v' with feature@head_errlog=disabled works"
log_onexit cleanup

truncate -s $MINVDEVSIZE $TESTDIR/vdev_a
log_must zpool create -f -o feature@head_errlog=disabled $TESTPOOL2 $TESTDIR/vdev_a

state=$(zpool list -Ho feature@head_errlog $TESTPOOL2)
if [[ "$state" != "disabled" ]]; then
	log_fail "head_errlog has state $state"
fi

log_must fio --rw=write --name=job --size=10M --filename=/$TESTPOOL2/10m_file
log_must zinject -t data -e checksum -f 100 -am /$TESTPOOL2/10m_file

# Try to read the file
dd if=/$TESTPOOL2/10m_file bs=1M of=/dev/null || true

log_must zfs snapshot $TESTPOOL2@snap
log_must zfs clone $TESTPOOL2@snap $TESTPOOL2/clone

# Check that snapshot and clone do not report the error.
log_mustnot eval "zpool status -v | grep '$TESTPOOL2@snap:/10m_file'"
log_mustnot eval "zpool status -v | grep '$TESTPOOL2/clone/10m_file'"
log_must eval "zpool status -v | grep '$TESTPOOL2/10m_file'"

# Check that enabling the feature reports the error properly.
log_must zpool set feature@head_errlog=enabled $TESTPOOL2
log_must zpool status -v $TESTPOOL2
log_must eval "zpool status -v | grep '$TESTPOOL2@snap:/10m_file'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone/10m_file'"
log_must eval "zpool status -v | grep '$TESTPOOL2/10m_file'"

log_pass "'zpool status -v' with feature@head_errlog=disabled works"
