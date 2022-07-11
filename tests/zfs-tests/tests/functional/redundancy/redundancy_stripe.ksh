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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2013, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/redundancy/redundancy.kshlib

#
# DESCRIPTION:
#	Striped pool have no data redundancy. Any device errors will
#	cause data corruption.
#
# STRATEGY:
#	1. Create N virtual disk file.
#	2. Create stripe pool based on the virtual disk files.
#	3. Fill the filesystem with directories and files.
#	4. Record all the files and directories checksum information.
#	5. Damage one of the virtual disk file.
#	6. Verify the data is error.
#

verify_runnable "global"

log_assert "Verify striped pool have no data redundancy."
log_onexit cleanup

typeset -i cnt=$(random_int_between 2 5)
setup_test_env $TESTPOOL "" $cnt

damage_devs $TESTPOOL 1 "keep_label"
log_must zpool scrub -w $TESTPOOL

if is_healthy $TESTPOOL ; then
	log_fail "$pool should not be healthy."
fi

log_pass "Striped pool has no data redundancy as expected."
