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
# Verify that mounting ZFS without a source fails with EINVAL.
#
# STRATEGY:
# 1. Create an empty mountpoint.
# 2. Issue mount(2) with a NULL source using mount_null_source.
# 3. Verify that mount(2) returns EINVAL.
#

log_assert "Mounting ZFS without a source fails with EINVAL"

typeset mountpoint="$TEST_BASE_DIR/null-source"

function cleanup
{
	if mounted "$mountpoint"; then
		log_must umount "$mountpoint"
	fi
	rm -rf "$mountpoint"
}

log_onexit cleanup
log_must mkdir "$mountpoint"
log_must mount_null_source "$mountpoint"

log_pass "ZFS rejected a mount without a source"
