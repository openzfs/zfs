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
