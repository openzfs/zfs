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
# Copyright 2026, tiehexue <tiehexue@hotmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that a filehandle-based reopen of an open-unlinked file
# succeeds. This tests that zfs_vget() / zfs_fhtovp() does not
# incorrectly reject znodes with z_unlinked when the inode/vnode
# is still referenced.
#
# STRATEGY:
# 1. Use the file_unlink_fh helper to create a file, obtain a
#    filehandle (name_to_handle_at on Linux, getfh on FreeBSD),
#    unlink the file, and then attempt to re-open it via
#    open_by_handle_at (Linux) / fhopen (FreeBSD).
# 2. Verify that the reopen succeeds.
#

verify_runnable "global"

function cleanup
{
	cd "$cwd" || true
	[[ -e $TESTDIR ]] && log_must rm -Rf $TESTDIR/*
}

log_assert "filehandle reopen of open-unlinked file should succeed"

log_onexit cleanup

cwd=$PWD
log_must cd $TESTDIR

# file_unlink_fh: creates file, gets filehandle, unlinks, reopens via
# open_by_handle_at (Linux) or fhopen (FreeBSD). Exits 0 on success.
log_must file_unlink_fh unlinked_fh_testfile

log_pass "filehandle reopen of open-unlinked file succeeded"
