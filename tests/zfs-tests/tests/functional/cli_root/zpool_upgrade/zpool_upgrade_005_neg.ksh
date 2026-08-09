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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
# Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zpool_upgrade/zpool_upgrade.kshlib

#
# DESCRIPTION:
# Variations of upgrade -v print usage message, return with non-zero status
#
# STRATEGY:
# 1. Execute the command with several invalid options
# 2. Verify a 0 exit status for each
#

verify_runnable "global"

log_assert "Variations of upgrade -v print usage message," \
    "return with non-zero status"

for arg in "/tmp" "-?" "-va" "-v fakepool" "-a fakepool" ; do
        log_mustnot zpool upgrade $arg
done

log_pass "Variations of upgrade -v print usage message," \
    "return with non-zero status"
