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

log_assert "ZFS supports RENAME_EXCHANGE."
log_onexit cleanup

cd $TESTDIR
echo "foo" > foo
echo "bar" > bar

# Self-exchange is a no-op.
log_must renameat2 -x foo foo
log_must grep '^foo$' foo

# Basic exchange.
log_must renameat2 -x foo bar
log_must grep '^bar$' foo
log_must grep '^foo$' bar

# And exchange back.
log_must renameat2 -x foo bar
log_must grep '^foo$' foo
log_must grep '^bar$' bar

# Exchange with a bad path should fail.
log_mustnot renameat2 -x bar baz

log_pass "ZFS supports RENAME_EXCHANGE as expected."
