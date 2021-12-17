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
# Copyright (c) 2023, George Amanakis. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	Verify error scrub clears the errorlog, if errors no longer exist.
#
# STRATEGY:
#	1. Create a pool with head_errlog disabled.
#	2. Run an error scrub and verify it is not supported.
#

verify_runnable "global"

function cleanup
{
	rm -f /$TESTPOOL2/$TESTFILE0
	destroy_pool $TESTPOOL2
}

log_onexit cleanup

log_assert "Verify error scrub cannot run without the head_errlog feature."

truncate -s $MINVDEVSIZE $TESTDIR/vdev_a
log_must zpool create -f -o feature@head_errlog=disabled $TESTPOOL2 $TESTDIR/vdev_a
log_mustnot zpool scrub -ew $TESTPOOL2

log_pass "Verify error scrub cannot run without the head_errlog feature."

