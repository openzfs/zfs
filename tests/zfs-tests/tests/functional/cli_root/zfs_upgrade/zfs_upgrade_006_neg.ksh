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
# Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zfs_upgrade/zfs_upgrade.kshlib

#
# DESCRIPTION:
# Verify that invalid upgrade parameters and options are caught.
#
# STRATEGY:
# 1. Create a ZFS file system.
# 2. For each option in the list, try 'zfs upgrade'.
# 3. Verify that the operation fails as expected.
#

verify_runnable "both"

set -A args "" "-?" "-A" "-R" "-b" "-c" "-d" "--invalid" \
    "-V" "-V $TESTPOOL/$TESTFS" "-V $TESTPOOL $TESTPOOL/$TESTFS"

log_assert "Badly-formed 'zfs upgrade' should return an error."

typeset -i i=1
while (( i < ${#args[*]} )); do
	log_mustnot zfs upgrade ${args[i]}
	((i = i + 1))
done

log_pass "Badly-formed 'zfs upgrade' fail as expected."
