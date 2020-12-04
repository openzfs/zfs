#!/bin/ksh -p
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright 2020 iXsystems, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# zfs create -u should leave the new file system unmounted.
# It should not work for a volume.
#
# STRATEGY:
# 1. Create a file system using -u and make sure the file system is not mounted.
# 3. Do it for a volume to verify it fails.
#

verify_runnable "both"

function cleanup
{
	local ds

	for ds in "$fs" "$vol"; do
		datasetexists "$ds" && destroy_dataset "$ds"
	done
}
log_onexit cleanup

log_assert "zfs create -u leaves the new file system unmounted"

typeset fs="$TESTPOOL/$TESTFS1"
typeset vol="$TESTPOOL/$TESTVOL1"

log_must create_dataset "$fs" "-u"
log_mustnot ismounted "$fs"

log_mustnot zfs create -V $VOLSIZE -u "$vol"

log_pass "zfs create -u leaves the new file system unmounted"
