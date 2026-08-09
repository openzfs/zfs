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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#

#
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
# Copyright 2015 Nexenta Systems, Inc.  All rights reserved.
#

. $STF_SUITE/tests/functional/cli_root/zpool_upgrade/zpool_upgrade.kshlib

#
# DESCRIPTION:
# Executing 'zpool upgrade -v' command succeeds, prints a description of legacy
# versions, and mentions feature flags.
#
# STRATEGY:
# 1. Execute the command
# 2. Verify a 0 exit status
# 3. Grep for version descriptions and 'feature flags'
#

verify_runnable "global"

function cleanup
{
	rm -f $versions
}

log_assert "Executing 'zpool upgrade -v' command succeeds"
log_onexit cleanup

typeset versions=$TEST_BASE_DIR/zpool-versions.$$

log_must zpool upgrade -v

# We also check that the usage message contains a description of legacy
# versions and a note about feature flags.

log_must eval "zpool upgrade -v | head -1 | grep 'feature flags'"

zpool upgrade -v > $versions

#
# Current output for 'zpool upgrade -v' has different indent space
# for single and double digit version number. For example,
#  9   refquota and refreservation properties
#  10  Cache devices
#
for version in {1..28}; do
	log_note "Checking for a description of pool version $version"
	log_must eval "awk '/^ $version / { print $1 }' $versions | grep $version"
done

log_pass "Executing 'zpool upgrade -v' command succeeds"
