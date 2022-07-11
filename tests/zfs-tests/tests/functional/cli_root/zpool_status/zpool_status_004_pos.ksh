#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
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
dd if=/$TESTPOOL2/10m_file bs=1M || true

log_must zfs snapshot $TESTPOOL2@snap
log_must zfs clone $TESTPOOL2@snap $TESTPOOL2/clone

# Check that snapshot and clone do not report the error.
log_mustnot eval "zpool status -v | grep '$TESTPOOL2@snap:/10m_file'"
log_mustnot eval "zpool status -v | grep '$TESTPOOL2/clone/10m_file'"
log_must eval "zpool status -v | grep '$TESTPOOL2/10m_file'"

# Check that enabling the feature reports the error properly.
log_must zpool set feature@head_errlog=enabled $TESTPOOL2
log_must eval "zpool status -v | grep '$TESTPOOL2@snap:/10m_file'"
log_must eval "zpool status -v | grep '$TESTPOOL2/clone/10m_file'"
log_must eval "zpool status -v | grep '$TESTPOOL2/10m_file'"

log_pass "'zpool status -v' with feature@head_errlog=disabled works"
