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
