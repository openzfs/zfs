#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_upgrade/zfs_upgrade.kshlib

#
# DESCRIPTION:
#	Executing 'zfs upgrade -v ' command succeeds, it should
#	show the info of available versions.
#
# STRATEGY:
# 1. Execute 'zfs upgrade -v', verify return value is 0.
# 2, Verify all the available versions info are printed out.
#

verify_runnable "both"

function cleanup
{
	if [[ -f $output ]]; then
		log_must rm -f $output
	fi
}

log_assert "Executing 'zfs upgrade -v' command succeeds."
log_onexit cleanup

typeset output=$TEST_BASE_DIR/zfs-versions.$$
typeset expect_str1="Initial ZFS filesystem version"
typeset expect_str2="Enhanced directory entries"

log_must eval 'zfs upgrade -v > /dev/null 2>&1'

zfs upgrade -v | awk '$1 ~ "^[0-9]+$" {print $0}'> $output
log_must grep -q "${expect_str1}" $output
log_must grep -q "${expect_str2}" $output

log_pass "Executing 'zfs upgrade -v' command succeeds."
