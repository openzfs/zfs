#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/block_cloning/block_cloning.kshlib

#
# DESCRIPTION:
#	Verify that when modifying and freeing cloned blocks after a top-level
#	vdev removal, there is no panic. This is a regression test for #17180.
#

verify_runnable "global"

export VDIR=$TEST_BASE_DIR/disk-bclone
export VDEV="$VDIR/0 $VDIR/1"
log_must rm -rf $VDIR
log_must mkdir -p $VDIR
log_must truncate -s $MINVDEVSIZE $VDEV

claim="No panic when destroying dataset with cloned blocks after top-level vdev removal"

log_assert $claim

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $TESTDIR $VDIR
}

log_onexit cleanup

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $VDEV
log_must dd if=/dev/urandom of=/$TESTPOOL/file bs=16M count=2
log_must zpool remove -w $TESTPOOL $VDIR/1
log_must zfs create $TESTPOOL/$TESTFS
log_must clonefile -f /$TESTPOOL/file /$TESTPOOL/$TESTFS/file
log_must dd if=/dev/urandom of=/$TESTPOOL/file bs=16M count=2
log_must zfs destroy -r $TESTPOOL/$TESTFS

log_pass $claim
