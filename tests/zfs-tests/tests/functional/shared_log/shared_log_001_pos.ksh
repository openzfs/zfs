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
# Copyright (c) 2023 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/shared_log/shared_log.kshlib

#
# DESCRIPTION:
#	Creating a pool with a shared log succeeds.
#
# STRATEGY:
#	1. Create shared log pool
#	2. Create client pool with shared log
#	3. Display pool status
#

verify_runnable "global"

log_assert "Creating a pool with a shared log succeeds."
log_onexit cleanup

log_must create_pool $LOGPOOL -L "$DISK0"
log_must create_pool $TESTPOOL -l $LOGPOOL "$DISK1"
log_must verify_shared_log $TESTPOOL $LOGPOOL

log_pass "Creating a pool with a shared log succeeds."
