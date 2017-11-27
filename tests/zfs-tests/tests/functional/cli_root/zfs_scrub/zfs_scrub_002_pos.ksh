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
# Copyright (c) 2017 Datto Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	Verify scrub shows the right status.
#
# STRATEGY:
#	1. Create pool and create 2 filesystems in it.
#	2. Write 256M to testfs1 and 512M to testfs2.
#	3. zfs scrub testfs2 and verify it's doing a dataset scrub.
#	4. Verify zpool scrub -s succeeds when the dataset is scrubbing.
#
# NOTES:
#	A 10ms delay is added to the ZIOs in order to ensure that the
#	scrub does not complete before it has a chance to be cancelled.
#	This can occur when testing with small pools or very fast hardware.
#
#	We expect zpool status to report 512M scrubbing, which matches the
#	bytes in testfs2.
#

verify_runnable "global"

function cleanup
{
	log_must zinject -c all
}

log_onexit cleanup

log_assert "Verify scrub and scrub -s show the right status."

log_must zinject -d $DISK1 -D20:1 $TESTPOOL
log_must zfs scrub $TESTPOOL/$TESTFS2
log_must is_dataset_scrubbing $TESTPOOL "512M" true
log_must zpool scrub -s $TESTPOOL
log_must is_pool_scrub_stopped $TESTPOOL true

log_pass "Verified scrub and -s show expected status."
