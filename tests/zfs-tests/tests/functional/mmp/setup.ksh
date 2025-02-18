#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2017 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/mmp/mmp.cfg

verify_runnable "global"

if [ -e $HOSTID_FILE ]; then
	log_unsupported "System has existing $HOSTID_FILE file"
fi

log_must set_tunable64 MULTIHOST_HISTORY $MMP_HISTORY
log_must set_tunable64 MULTIHOST_INTERVAL $MMP_INTERVAL_DEFAULT
log_must set_tunable64 MULTIHOST_FAIL_INTERVALS $MMP_FAIL_INTERVALS_DEFAULT

log_pass "mmp setup pass"
