#!/bin/ksh -p
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
