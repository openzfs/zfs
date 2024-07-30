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
# Copyright (c) 2024 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/shared_log/shared_log.kshlib

#
# DESCRIPTION:
#	Test fault behavior of shared log pool
#
# STRATEGY:
#	1. Create shared log pool & client
#	2. Fault the provider pool
#	3. Verify the client pool also faults
#

verify_runnable "global"

log_assert "Test fault behavior of shared log pools."
log_onexit cleanup

typeset FS="$TESTPOOL/fs"

log_must create_pool $LOGPOOL -L "$DISK0"
log_must create_pool $TESTPOOL -l $LOGPOOL "$DISK1"
log_must zinject -d "$DISK0" -A degrade $LOGPOOL
log_must eval "zpool status -e $TESTPOOL | grep DEGRADED"

log_pass "Test fault behavior of shared log pools."
