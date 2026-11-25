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
