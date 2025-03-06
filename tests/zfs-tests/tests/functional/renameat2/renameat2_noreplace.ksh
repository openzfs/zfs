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
# Copyright (C) 2019 Aleksa Sarai <cyphar@cyphar.com>
# Copyright (C) 2019 SUSE LLC
#

. $STF_SUITE/include/libtest.shlib

verify_runnable "both"

function cleanup
{
	log_must rm -rf $TESTDIR/*
}

log_assert "ZFS supports RENAME_NOREPLACE."
log_onexit cleanup

cd $TESTDIR
touch foo bar

# Clobbers should always fail.
log_mustnot renameat2 -n foo foo
log_mustnot renameat2 -n foo bar
log_mustnot renameat2 -n bar foo

# Regular renames should succeed.
log_must renameat2 -n bar baz

log_pass "ZFS supports RENAME_NOREPLACE as expected."
