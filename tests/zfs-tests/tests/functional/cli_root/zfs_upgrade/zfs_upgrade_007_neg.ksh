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
# Verify that version should only by '1' '2' or current version,
# non-digit input are invalid.
#
# STRATEGY:
# 1. For each invalid value of version in the list, try 'zfs upgrade -V version'.
# 2. Verify that the operation fails as expected.
#

verify_runnable "both"

set -A args \
	"0" "0.000" "0.5" "-1.234" "-1" "1234b" "5678x"

log_assert "Set invalid value or non-digit version should fail as expected."

typeset -i i=0
while (( i < ${#args[*]} ))
do
	log_mustnot zfs upgrade -V ${args[i]} $TESTPOOL/$TESTFS
	((i = i + 1))
done

log_pass "Set invalid value or non-digit version fail as expected."
