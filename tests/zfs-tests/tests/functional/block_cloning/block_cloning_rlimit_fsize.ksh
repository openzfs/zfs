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
#	When block cloning is used to implement copy_file_range(2), the
#	RLIMIT_FSIZE limit must be respected.
#
# STRATEGY:
#	1. Create a pool.
#	2. ???
#

verify_runnable "global"

VDIR=$TEST_BASE_DIR/disk-bclone
VDEV="$VDIR/a"

function cleanup
{
	datasetexists $TESTPOOL && destroy_pool $TESTPOOL
	rm -rf $VDIR
}

log_onexit cleanup

log_assert "Test for RLIMIT_FSIZE handling with block cloning enabled"

log_must rm -rf $VDIR
log_must mkdir -p $VDIR
log_must truncate -s 1G $VDEV

log_must zpool create -o feature@block_cloning=enabled $TESTPOOL $VDEV

log_must dd if=/dev/urandom of=/$TESTPOOL/file1 bs=1 count=1000

ulimit -f 2
log_must clonefile -f /$TESTPOOL/file1 /$TESTPOOL/file2 0 0 all
ulimit -f 1
log_mustnot clonefile -f /$TESTPOOL/file1 /$TESTPOOL/file3 0 0 all

log_pass "copy_file_range(2) respects RLIMIT_FSIZE"
