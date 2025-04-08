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

#
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
#

# DESCRIPTION:
#	Verify zpool status command mode (-c) respects ZPOOL_SCRIPTS_ENABLED.
#
# STRATEGY:
#	1. Set ZPOOL_SCRIPTS_ENABLED to 0, disabling zpool status -c
#	2. zpool status -c must not run successfully
#	3. Set ZPOOL_SCRIPTS_ENABLED to 1, enabling zpool status -c
#	4. zpool status -c must run successfully
#	5. Unset ZPOOL_SCRIPTS_ENABLED, enabling zpool status -c
#	6. zpool status -c must run successfully

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/zpool_script.shlib

verify_runnable "both"

log_assert "zpool status -c properly handles ZPOOL_SCRIPTS_ENABLED"

export ZPOOL_SCRIPTS_ENABLED=0
log_mustnot zpool status -c media

export ZPOOL_SCRIPTS_ENABLED=1
log_must zpool status -c media

unset ZPOOL_SCRIPTS_ENABLED
log_must zpool status -c media

log_pass "zpool status -c properly handles ZPOOL_SCRIPTS_ENABLED passed"
