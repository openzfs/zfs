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
