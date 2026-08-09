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

log_assert "ZFS supports RENAME_WHITEOUT."
log_onexit cleanup

cd $TESTDIR
echo "whiteout" > whiteout

# Straight-forward rename-with-whiteout.
log_must renameat2 -w whiteout new
# Check new file.
log_must grep '^whiteout$' new
# Check that the whiteout is actually a {0,0} char device.
log_must grep '^character special file:0:0$' <<<"$(stat -c '%F:%t:%T' whiteout)"

log_pass "ZFS supports RENAME_WHITEOUT as expected."
