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

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Verify that datasets mounted at directories with whitespace are properly escaped
# both going in (for mountd consumption) and going out (for removing from export list)
#
# STRATEGY:
# 1. Create and share a dataset with spaces, tabs, and newlines
# 2. Verify it's shared
# 3. Unshare it
# 4. Verify it's not shared
#

shares_can_have_whitespace || log_unsupported "Platform doesn't permit whitespace in NFS shares"
basename='a + b
 c	d'
escname='a\040+\040b\012\040c\011d'

verify_runnable "global"

function cleanup
{
	datasetexists "$TESTPOOL/$TESTFS/shared1" && \
		destroy_dataset "$TESTPOOL/$TESTFS/shared1" -f
}

log_assert "Datasets with spaces are properly shared and unshared."
log_onexit cleanup

log_must    zfs create -o sharenfs=on -o mountpoint="$TESTDIR/$basename" "$TESTPOOL/$TESTFS/shared1"
log_must    is_shared "$TESTDIR/$escname"
log_must    zfs unshare "$TESTPOOL/$TESTFS/shared1"
log_mustnot is_shared "$TESTDIR/$escname"

log_pass "Datasets with spaces are properly shared and unshared."
