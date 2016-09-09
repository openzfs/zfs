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
# Copyright (c) 2012 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
#	Resilver prevent scrub from starting until the resilver completes
#
# STRATEGY:
#	1. Setup a mirror pool and filled with data.
#	2. Detach one of devices
#	3. Verify scrub failed until the resilver completed
#
# NOTES:
#	A 1 second delay is added to 10% of zio's in order to ensure that
#	the resilver does not complete before the scrub can be issue.  This
#	can occur when testing with small pools or very fast hardware.

verify_runnable "global"

log_assert "Resilver prevent scrub from starting until the resilver completes"

log_must $ZINJECT -d $DISK1 -f10 -D1 $TESTPOOL
log_must $ZPOOL detach $TESTPOOL $DISK2
log_must $ZPOOL attach $TESTPOOL $DISK1 $DISK2
log_must is_pool_resilvering $TESTPOOL
log_mustnot $ZPOOL scrub $TESTPOOL

# Allow the resilver to finish, or it will interfere with the next test.
while ! is_pool_resilvered $TESTPOOL; do
	$SLEEP 1
done

log_must $ZINJECT -c all
log_pass "Resilver prevent scrub from starting until the resilver completes"
