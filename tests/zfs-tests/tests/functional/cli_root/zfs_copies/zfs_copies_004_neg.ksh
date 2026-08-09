#!/bin/ksh
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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_copies/zfs_copies.kshlib

#
# DESCRIPTION:
#	Verify that copies cannot be set to other value except for 1, 2 or 3
#
# STRATEGY:
#	1. Create filesystems with copies set as any value other than 1, 2 or 3
#	2. Verify that the create operations fail
#

verify_runnable "both"

log_assert "Verify that copies property cannot be set to any value other than 1,2 or 3"

set -A badval 0 01 02 03 0 -1 -2 -3 10 20 30 4 5 6 blah

for val in ${badval[@]}; do
	log_mustnot zfs create -o copies=$val $TESTPOOL/$TESTFS1
	log_mustnot zfs create -V $VOLSIZE -o copies=$val $TESTPOOL/$TESTVOL1
	log_mustnot zfs set copies=$val $TESTPOOL/$TESTFS
	log_mustnot zfs set copies=$val $TESTPOOL/$TESTVOL
	block_device_wait
done

log_pass "The copies property cannot be set to any value other than 1,2 or 3 as expected"
